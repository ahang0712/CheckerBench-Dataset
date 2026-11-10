---
name: build-compile-commands
description: Use when building a compile_commands.json for a project (Linux kernel or autotools/CMake) to enable refinement subsystem scanning or clang --analyze — covers build_refinement_repo.py usage, Kbuild specifics, and the traps (shell-quoted -D flags, -c -o parsing, gcc pollution, generated headers).
---

# Building compile_commands.json for Refinement Scanning

## Goal

精炼扫描（`scan_subsystem`）需要一个项目 **main 分支代码**自己的
`compile_commands.json`（`<repo>/refinement-main/compile_commands.json`），
用 clang 编译（flags 与 clang --analyze 兼容），并能被
`cve_checker_synth/csa/refinement.py` 直接消费。

## 两条路径

### 路径 A：autotools / CMake 类项目（gpac 等）

用 `tools/build_refinement_repo.py --project <db_project_id>`：

1. `ensure_worktree`：`<repo>/refinement-main` 持久 worktree（优先 main 分支，
   无 main 自动 fallback HEAD）
2. `install_wrappers`：`/tmp/refinement-wrappers` 下 clang/gcc 伪装脚本，
   注入 `-MJ` fragment（`WRAPPER_SH` 模板）
3. configure（`CC=clang CFLAGS=-O0 -g`）→ `make clean` → `make -j8`
   （**必须 make clean**：增量 make 只产出部分 fragment）
4. `merge_fragments`：`-MJ` 输出是 JSON 数组元素（带尾逗号），拼接去尾逗号

已实测：gpac 448 entries（src/ 378），0 编译错误。

### 路径 B：Linux 内核（Kbuild）

`build_refinement_repo.py` 检测到 Kbuild 特征（`scripts/Kconfig.include` 存在）
自动走 `build_kernel()`：

1. worktree 创建同路径 A
2. **复用源 checkout 的 `.config`**（`shutil.copy`）——避免 defconfig 漂移；
   若源 repo 已有编译产物，也复用 `scripts/kconfig/{lexer.lex.c,parser.tab.c,parser.tab.h}`
   （容器缺 flex/bison 时绕过）
3. `make -j96 CC=clang`（全量编译，clang 条目；失败目录可接受，条目会缺失）
4. 官方 `make compile_commands.json`（依赖 `vmlinux.a` + `MODORDER`，
   **完整构建成功**才可用）；构建不完整时改手动：
   `cd <worktree> && python3 scripts/clang-tools/gen_compile_commands.py -a ar -o compile_commands.json`
   （**必须 cd 到 worktree 内运行**——脚本扫描 cwd 的 `.cmd` 文件）
5. **过滤 gcc 条目**（host 工具 / 误编产物）：只保留 command 以 `clang` 开头的

实测：内核 32137 → 过滤后 26394 条 clang 条目（旧 gcc 快照仅 2792）。

## 已知坑与对策（按踩坑顺序）

### 1. 构建依赖（容器缺包）

内核 Kconfig 需要 **flex + bison**（syncconfig 生成 lexer/parser，`FORCE` 强制重建，
复制生成物无效）；objtool 需要 **libelf-dev**（gelf.h）；timeconst 需要 **perl**；
部分配置要 **bc**。Ubuntu：`sudo apt-get install -y flex bison libelf-dev bc`。

### 2. CONFIG_WERROR=y 让 clang 18 翻车

旧 .config 按老编译器写的，clang 18 的新警告（如
`-Wconstant-conversion`）被 `-Werror` 变 error → fs/cifs、lib 等目录失败。
对策：`.config` 里 `CONFIG_WERROR=y` → `n`，然后 `make olddefconfig` 同步。
另注意 `CONFIG_IKHEADERS=m` 时 `kernel/kheaders_data.tar.xz` 可能 Error 127
（tar 链问题）——一并 `CONFIG_IKHEADERS=n`。

### 3. make 不带 CC=clang 会污染 .cmd

Kbuild 的 `.cmd` 文件（savedcmd）记录**上一次编译的实际命令**。
一旦用 `make xxx CC=clang` 之外的调用（如 `make lib/raid6/` 不带 CC=clang、
`make compile_commands.json` 触发依赖链），缺失文件会被 **gcc 补编**，
`.cmd` 变成 gcc 的 → gen_compile_commands 混入 gcc 条目（clang 不认
`-mpreferred-stack-boundary=3` 等 gcc-only flag）。对策：**所有手动 make
一律带 `CC=clang`**；事后过滤 `command` 以 gcc 开头的条目。

### 4. `make compile_commands.json` 依赖完整构建

其依赖是 `vmlinux.a $(KBUILD_VMLINUX_LIBS) $(MODORDER)`——任何目录失败
（如 lib/raid6）都会让它报 `Makefile:2026: . Error 2` 且不产出文件。
精炼只需要 `.cmd` 文件而非完整链接产物 → **绕过依赖链手动跑
gen_compile_commands.py**（见路径 B 第 4 步）。

### 5. 中断的 make 会删 include/generated 生成头

`make syncconfig`（olddefconfig 同步）会**先删后建** autoconf.h；
中途 kill make 会留下 `include/generated/` 缺 `autoconf.h` 等空态 →
之后所有文件 `'generated/autoconf.h' file not found`。
对策：`make -C <wt> prepare CC=clang` 重建生成头。（`build-salt.h` 缺失
只影响 `.mod.c` 生成文件，可忽略。）

### 6. `_entry_compile_flags` 解析 command 的两个坑

`scan_subsystem` 用 `_entry_compile_flags` 把编译命令转成 analyze flags：

- **`-c -o out src` 顺序**（内核 command 风格）：`-c` 是裸 flag 不带值，
  `-o` 带值。逐 token 处理时若对 `-c` 也 skip 下一个参数，会吃掉 `-o`
  本身，导致 `out` 泄漏进 flags（clang 报 `'linker' input unused`）。
  对策：`-c` → `i += 1`；`-o` → `i += 2`。
- **Kbuild savedcmd 保留单引号**：`.cmd` 里存的是 shell 展开前的原始命令
  （`-DKBUILD_MODNAME='"veth"'`），`command.split()` 不做 shell 解析，
  单引号残留让宏定义变成 `'"veth"'` → 展开崩溃（`NL_SET_ERR_MSG` 链 →
  `array initializer must be an initializer list`）。对策：
  `args = [a.replace("'", "") for a in args]`（对 command.split() 结果）。

### 7. 路径与目录陷阱

- **容器路径翻译**：DB 存宿主路径 `/data2/stu_Wangli/repo/...`，容器内是
  `/workspace/repo/...`。`config.json` 的 `repo_path` 必须是**容器路径**
  （`scan_subsystem` 的 `_main_worktree_path` 直接拼接）。
- **gen_compile_commands.py 扫描 cwd**：在项目根跑会产出 0 条；
  必须 cd 到 worktree。
- **`reset --hard HEAD` 清空构建产物**：`ensure_worktree` 的 update 分支
  （fetch origin main 失败后 fallback）会 reset worktree，抹掉 .config /
  include/generated / .o——**重跑 build_refinement_repo.py 前要知道会全量重建**。
- **`-Wp,-MMD,xxx` 与 `-fsanitize=*` 不影响 --analyze**：可保留，
  报错源头通常是宏定义而非这些 flags。

### 8. 编译条目里不能扫的

- `.mod.c` 生成文件（缺 build-salt.h 等）→ 编译错误，计入 n_compile_error，无害
- `arguments` 字段：`gen_compile_commands.py` 生成的条目**只有 `command` 字段**
  （无 `arguments`）——`_entry_compile_flags` 有 command fallback，没问题

## 验证清单（做完必跑）

```bash
# 1. 条目数与编译器分布
python3 -c "import json; d=json.load(open('<wt>/compile_commands.json')); print(len(d))"
# 2. 单文件 clang --analyze + checker 加载（用任一已合成 checker.so）
#    —— 期望 rc=0 无 error（可无 warning）
# 3. scan_subsystem 子系统 smoke（临时 workdir + config.json 指向目标 repo）
python3 -c "
from cve_checker_synth.csa.refinement import scan_subsystem, extract_checker_name
w, d = scan_subsystem('/tmp/smoke-wd', '/tmp/smoke-wd/checker.so', 'your.Checker', '<subsystem-prefix>/', max_envs=20)
print(d)  # n_compile_error 应为 0
"
```

## 参考

- 工具：`tools/build_refinement_repo.py`（路径 A + Kbuild 分支）
- 消费者：`cve_checker_synth/csa/refinement.py::scan_subsystem` /
  `_entry_compile_flags`（单引号剥离、-c/-o 处理在此实现）
- 实测记录：gpac 448 条 / 0 错；Linux 26394 条 / 0 编译错误（drivers/net 3751 条）
