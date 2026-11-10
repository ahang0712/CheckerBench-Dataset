# Task: build and maintain the CWE skill knowledge base

你正在执行的是 CVE-Driven CSA Checker 合成流水线中的一项支撑性工程：基于 CVE 数据库标注的
CWE 标签，构建一套仓库级共享的"漏洞类别知识库"。目的是让后续每个 CVE commit pair 任务在
七阶段 checker 合成流程开始时，就能直接拿到"这一类漏洞通常长什么样、CSA checker 该怎么设计、
有哪些已知误报陷阱"的先验知识，从而减少 CC 在合成阶段靠试错摸索这些经验所消耗的轮次。

这套知识库和此前的 `repo_knowledge.md`（仓库轴）是两条独立、并列存在的知识体系，互不干扰，分别
解决"这个仓库有什么特殊约定"和"这一类漏洞有什么通用模式"两个不同问题。两者都会以文件形式
存在于每个任务工作目录的 `references/` 下，但本任务只负责后者。

**数据前提**：当前数据库中每个 CVE commit pair 只携带一个 CWE 标签（不存在一个 pair 对应
多个 CWE 的情况）。因此本文件后续涉及的查找、拉取、落地逻辑均按"单标签"设计，不需要处理
多标签合并或排序的情形；若未来数据源发生变化、出现多标签 pair，需要重新评估第三、四部分的
查找逻辑是否要扩展为遍历多个标签。

**卡片内容的定位**：本知识库以 **CWE 官方定义 + CC 自身对 C/C++ 静态分析领域的通用知识**
为基础撰写，提供的是该类漏洞的领域通用认知，不绑定任何具体仓库或具体 commit 的实际验证
结果。这是一个明确且固定的范围设定：知识库不随任务运行积累的 `summary.json` 做增量更新，
撰写时请保持自知，卡片里不应出现任何暗示"我们已经在这些项目中验证过"的措辞。

---

## 0. 输入材料

你将基于一份 CWE 分布统计报告（字段包括：每个 CWE 编号的全局样本数、占比、所属 Category 分组、
按项目拆分的样本数、以及 Linux 与非 Linux 项目的对比占比）来驱动整个分级与生成过程。**不要在
没有这份统计报告的情况下凭空猜测分布**——粒度分级的每一步判断都必须有数据支撑，这是本任务区别
于 `repo_knowledge.md` 生成任务的关键：repo_knowledge 允许通过抽样读代码来归纳约定，但 CWE 分级必须
先看统计数字再决定结构，顺序不能颠倒。

---

## 第一部分：三层分级规则（确定性算法，照此执行，不要自由发挥）

对统计报告中出现的每一个 CWE 编号（`NVD-CWE-noinfo` 和 `NVD-CWE-Other` 除外，这两个特殊标签
见第四部分单独处理），按以下顺序判定其所属层级：

### 判定流程

1. **第一步，检查全局样本数 N**：
   - 若 N ≥ 100，进入"第二步"判断是否需要做仓库变体拆分，最终归为 **Tier 1**
   - 若 20 ≤ N < 100，归为 **Tier 2**，跳过仓库变体判断
   - 若 N < 20，归为 **Tier 3**，进入"第三步"按 Category 归并

2. **第二步（仅 Tier 1 候选执行）：检查仓库分裂程度**：
   - 计算该 CWE 的 `Linux 占比` 与 `非 Linux 占比` 的比值（取较大值除以较小值）
   - 若比值 ≥ 2.0，判定为"存在仓库相关的模式分裂"，该 CWE 需要拆分为两份变体卡片：
     `CWE-{id}-kernel.md` 和 `CWE-{id}-userspace.md`
   - 若比值 < 2.0，该 CWE 维持 Tier 1 但只生成单一卡片 `CWE-{id}.md`，不做仓库拆分
   - 注意：即使某个 CWE 样本量落在 Tier 2 区间（20-99），只要它的 Linux/非 Linux 占比比值
     极端偏向某一侧（例如比值 ≥ 10，如 race condition、improper locking 这类近乎仓库专属的
     类别），也应当在卡片正文中明确标注"该类别样本几乎全部来自内核类项目，在用户态库中的
     适用性未经验证"，即使不生成独立的变体文件

3. **第三步（仅 Tier 3 执行）：按 Category 归并**：
   - 将所有 N < 20 的 CWE，按统计报告中给出的 Category 分组（通常包括 Memory Safety、
     Resource Management、Integer/Arithmetic、Concurrency、Input Validation、
     Security Policy、Type/Misc 等）合并进对应的伞形卡片 `Category-{name}.md`
   - 一个 Tier 3 编号只能归入一个 Category，若统计报告中该编号未明确归类，归入
     `Category-Unclassified.md`
   - 同一 Category 下的多个低频 CWE 共享一张卡片，卡片内部用小节区分不同编号，但不需要
     像 Tier 1/2 那样写满完整结构（见第二部分 2.3）

### 分级结果不是一次性的

每当数据集新增更多 commit pair、统计报告刷新后，重新跑一遍上述三步判定。某个 CWE 可能因为
样本量增长而从 Tier 3 晋升到 Tier 2，甚至 Tier 1。晋升时按"第二部分"重新生成对应粒度的卡片，
并更新 `index.json`；但**不要因为晋升就丢弃旧卡片里已经积累的误报经验**——把 Tier 3 阶段
积累的相关内容迁移合并进新生成的独立卡片里，而不是从零重写。

---

## 第二部分：卡片内容生成规范

### 2.1 Tier 1 卡片结构（含仓库变体时，两份变体各自独立生成，结构相同）

```markdown
# CWE-{id}: {name}
> 粒度: Tier 1{ — kernel variant / — userspace variant，若有拆分}
> 全局样本数: {N} | 本变体样本数: {N_variant} | 来源项目数: {project_count}

## 1. 典型代码模式
（基于 CWE 官方定义，结合该编号在 C/C++ 语境下的常见实现形态做具体化描述，
 而不是停留在官方定义的抽象措辞。例如 CWE-416 在 kernel 语境下：通用知识
 表明此类漏洞常出现在某资源指针被释放后，另一条执行路径仍持有该指针并继续
 访问，在内核场景下这条"另一条路径"经常是中断处理或异步回调。撰写时用
 "常见于""通常表现为"等措辞，不要写成"我们在此仓库中观察到"这类暗示有
 真实样本支撑的表述。）

## 2. CSA checker 架构建议
（应使用哪种 ento::Checker 基类、需要追踪的 SymbolRef/ProgramState 粒度，
 基于该类漏洞的一般静态分析原理给出建议。与 references/csa_checker_patterns.md
 中的具体模式名做交叉引用，不要重复展开该文件已有的内容，只指明"建议参考
 pattern X"。）

## 3. 已知高发误报模式
（基于静态分析领域对该类 checker 的通用认知，说明这类检测逻辑在原理上容易
 在什么情况下产生误报，建议在哪个分析阶段加什么样的校验来抑制。内容应基于
 CSA 静态分析的一般原理与该类漏洞模式的固有特性撰写，不依赖任何项目实测
 案例。）

## 4. 通用判别特征
（帮助 CC 快速判断"当前 patch 是否是教科书式的这一类 CWE，还是变种"。
 此部分必须保持仓库无关的抽象描述，不要掺杂任何具体仓库的实现细节——
 仓库专属信息属于 repo_knowledge.md 的职责，这里重复会造成两份文件不同步。）

## 5. 仓库分布参考
（若为非拆分卡片，列出 Top 3-5 高频出现该 CWE 的项目名，帮助 CC 建立预期；
 若为 kernel/userspace 变体卡片，说明该变体对应哪些典型项目类型。）

---
*Tier: 1 | Generated: {date} | Source report: {report_date}*
```

### 2.2 Tier 2 卡片结构（精简版，不含"仓库分布参考"小节，篇幅控制在 Tier 1 的 60% 左右）

```markdown
# CWE-{id}: {name}
> 粒度: Tier 2 | 全局样本数: {N}
> {若该 CWE 仓库占比比值 ≥ 10，在此处加一行警示，例如：
>  "注意：该类别样本 95% 以上来自内核类项目，用户态场景下的模式未经充分验证。"}

## 1. 典型代码模式
## 2. CSA checker 架构建议
## 3. 已知高发误报模式
## 4. 通用判别特征

---
*Tier: 2 | Generated: {date} | Source report: {report_date}*
```

### 2.3 Tier 3 伞形卡片结构（一张卡片覆盖一个 Category 下的多个低频 CWE）

```markdown
# Category: {category_name}
> 粒度: Tier 3（伞形）| 覆盖 CWE: {list_of_ids}
> 注意：本卡片覆盖的每个 CWE 全局样本数均 < 20，以下内容基于有限样本归纳，
> 置信度低于 Tier 1/2 卡片，请结合 patch.diff 的实际内容审慎参考，不要
> 机械套用。

## CWE-{id_1}: {name_1}
- 典型模式: （一两句话概括，不需要完整四段结构）
- checker 建议: （若样本太少无法给出具体建议，明确写"样本不足，暂无可靠经验"）

## CWE-{id_2}: {name_2}
- ...

（依次列出本 Category 下归并的所有 CWE，每个 3-5 行，不展开误报模式分析，
 除非某个低频 CWE 恰好有一条非常明确、值得记录的历史教训。）

---
*Tier: 3 | Generated: {date} | Source report: {report_date}*
```

### 2.4 内容生成时的信息来源与禁止事项

- **唯一信息源**：CWE 官方定义（可联网检索 cwe.mitre.org 上对应编号的页面以确保描述
  准确）加上 CC 自身对 C/C++ 静态分析、CSA `clang::ento` API 的通用知识。不参考、不依赖
  任何项目实测的历史任务产出
- **禁止编造**：不要虚构具体的项目名、文件名、commit 引用作为"案例证据"。如果某个细节
  缺乏官方定义或通用知识支撑，宁可写得抽象一些，也不要编造看似具体实则无依据的细节
- 不要在卡片中引用任何仓库专属的函数名、宏名、文件路径——一旦发现自己在写这类内容，说明
  这条信息应该归入 `repo_knowledge.md` 而不是这里，请移除
- Tier 1/2 卡片单篇控制在 80-120 行以内，Tier 3 伞形卡片整体控制在 150 行以内（因为要容纳
  多个 CWE），超出时优先精简"典型代码模式"的描述长度，而不是省略"已知高发误报模式"

---

## 第三部分：index.json 结构与查找逻辑

### 3.1 index.json 字段定义

```json
{
  "CWE-416": {
    "tier": 1,
    "has_repo_variant": true,
    "variants": {
      "kernel": "CWE-416-kernel.md",
      "userspace": "CWE-416-userspace.md"
    },
    "kernel_repo_match": ["torvalds_linux"]
  },
  "CWE-787": {
    "tier": 1,
    "has_repo_variant": true,
    "variants": {
      "kernel": "CWE-787-kernel.md",
      "userspace": "CWE-787-userspace.md"
    },
    "kernel_repo_match": ["torvalds_linux"]
  },
  "CWE-401": {
    "tier": 2,
    "has_repo_variant": false,
    "card_path": "CWE-401.md"
  },
  "CWE-682": {
    "tier": 3,
    "has_repo_variant": false,
    "card_path": "Category-Integer_Arithmetic.md",
    "parent_category": "Integer/Arithmetic"
  }
}
```

- `kernel_repo_match` 字段维护一份"内核类项目"白名单，用于变体查找时判断当前仓库该归入
  kernel 还是 userspace 侧。初始状态下该列表只需包含 `torvalds_linux`；若后续数据集引入
  其他内核级项目（如 BSD 系列、其他 RTOS），在此追加，不需要改动分级算法本身
- Tier 3 的多个 CWE 编号会指向同一个 `card_path`，这是预期行为，查找时不需要去重处理，
  直接定位到对应伞形卡片即可

### 3.2 任务时的查找与拉取逻辑（写给 GenericWorkdirPreparer 或负责组装 ScanEnv 的脚本）

当前 CVE commit pair 携带唯一的一个 CWE 标签（见前文"数据前提"），按以下流程查找拉取：

1. 在 `index.json` 中查找该 CWE 编号
2. 若 `tier == 1` 且 `has_repo_variant == true`：检查当前仓库名是否出现在该编号的
   `kernel_repo_match` 列表中，命中则拉取 `variants.kernel`，否则拉取 `variants.userspace`
3. 若 `tier == 1` 且 `has_repo_variant == false`，或 `tier == 2`：直接拉取 `card_path`
4. 若 `tier == 3`：拉取该编号对应的 `card_path`（伞形卡片）
5. 若该 CWE 编号未出现在 `index.json` 中（新出现的、尚未分级的编号）：跳过拉取，不要
   报错阻断任务，该任务工作目录的 `references/cwe_cards/` 下留空即可，并在 `summary.json`
   中标记此情况，便于后续将该编号纳入下一轮分级
6. 将命中的唯一一张卡片拷贝至 `{{WORKDIR}}/references/cwe_cards/` 下，与 `repo_knowledge.md`
   并列存放，不做合并

---

## 第四部分：NVD-CWE-noinfo 与 NVD-CWE-Other 的特殊处理

这两个标签不代表真实的漏洞类别，而是 NVD 数据库"未分类"或"分类不明确"的标记，无法像常规
CWE 那样归纳"典型代码模式"，因此不纳入第一部分的三层分级算法，单独生成一份诊断性质的引导
文件 `CWE-unclassified-diagnosis.md`，内容方向与常规 CWE 卡片完全不同：它不提供漏洞模式
先验，而是指导 CC 在缺乏 CWE 先验的情况下，如何从 `patch.diff` 本身反推漏洞性质。

### 4.1 该文件应包含的内容

```markdown
# Diagnosing unclassified CVEs (NVD-CWE-noinfo / NVD-CWE-Other)

> 当前 CVE pair 的 CWE 标签缺乏明确分类。本文件不提供漏洞模式先验，而是引导你
> 通过分析 patch.diff 本身的改动特征，反推该漏洞最可能归属的常规 CWE 类别，
> 从而能够进一步参考 references/cwe_cards/ 下对应类别的卡片（如果存在）。

## 1. 从 diff 改动类型反推漏洞性质的判断清单

逐项检查 patch.diff 的改动是否符合以下特征，命中后可参考对应 CWE 的常规卡片：

- diff 新增了边界检查（如新增 `if (idx >= len)` 类条件）→ 可能是 CWE-787/125
  （越界写/读），建议参考对应卡片
- diff 新增了空指针判断（如新增 `if (!ptr) return` 类条件）→ 可能是 CWE-476
- diff 调整了 free/release 调用的位置或新增置空操作（如 free 后补 `ptr = NULL`）
  → 可能是 CWE-416（UAF）或 CWE-415（Double Free），需结合具体改动方向判断
- diff 新增了整数运算前的溢出检查 → 可能是 CWE-190
- diff 调整了锁的获取/释放顺序或新增锁保护 → 可能是 CWE-362/CWE-667
- diff 修改了内存分配的大小计算逻辑 → 可能是 CWE-131 或 CWE-122/121
- 以上均不命中 → 该漏洞可能不属于典型内存安全范畴，建议直接基于 commit message
  和函数语义独立分析，不强行套用某个 CWE 模式

## 2. 推断后的下一步

完成上述判断后，若推断出较高置信度的类别，主动到 references/cwe_cards/ 目录下
查找是否已有对应卡片被拉取；若未被拉取（因为原始标签是 noinfo/Other，索引查找
阶段不会自动命中常规 CWE），可以基于本文件第一节的判断结果，自行参考该类别的
通用模式认知，但不要在 summary.json 中将其报告为"已确认的 CWE 标签"，而应
标注为"基于 diff 特征的推测类别"，保留原始标签的不确定性。

## 3. 禁止事项

不要仅因为缺少 CWE 先验就降低分析严谨性或跳过 before/after 验证步骤；本文件的
作用是补充先验缺失，不改变七阶段流程本身的验证要求。
```

### 4.2 该文件的存放与拉取规则

`CWE-unclassified-diagnosis.md` 是固定的静态文件，不随数据分布刷新而改变内容（除非判断
清单本身需要扩充新的 diff 特征模式）。当 `GenericWorkdirPreparer` 检测到当前 pair 的唯一 CWE
标签为 `NVD-CWE-noinfo` 或 `NVD-CWE-Other` 时，直接拉取此文件至 `references/cwe_cards/`，
不经过 `index.json` 查找流程。由于一个 pair 只携带一个标签，这条路径与第三部分 3.2 的
常规 CWE 查找路径互斥，不会同时触发。

---

## 第五部分：生成顺序建议

实际执行本任务时，按以下顺序进行，便于中途检查中间结果是否合理：

1. 解析统计报告，跑通第一部分的三层分级算法，先只输出分级结果（每个 CWE 对应的 tier、
   是否需要仓库变体），不急于生成卡片正文内容，人工或自动校验这份分级表是否符合预期
2. 分级表确认无误后，按第二部分的结构规范，逐个 tier 批量生成卡片正文
3. 生成 `index.json`，确保每张卡片都被正确索引，且 `kernel_repo_match` 等元字段填写完整
4. 单独生成 `CWE-unclassified-diagnosis.md`
5. 抽样选择 2-3 个不同 tier 的真实 CVE pair，手动模拟一次"第三部分 3.2"的查找拉取流程，
   确认 `references/cwe_cards/` 下最终落地的文件符合预期，再批量应用到全部任务工作目录

本知识库为一次性静态生成产物：内容完全基于 CWE 官方定义与 C/C++ 静态分析领域的通用知识，
不随任务运行积累的 `summary.json` 做增量更新。后续若分布统计报告刷新（数据集新增更多
commit pair），仅重新执行第一部分的分级判定与第二部分的卡片重新生成，不涉及样本驱动的
内容增强环节。
