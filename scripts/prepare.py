#!/usr/bin/env python3
"""Create a writable checker workspace from the immutable benchmark export."""
import argparse
import json
import os
import re
import hashlib
import shlex
from pathlib import Path
import shutil
import subprocess

ROOT = Path(os.environ.get('CHECKERBENCH_DATA_ROOT', '/data'))
CSA = ROOT / 'csa159'
CODEQL = os.environ.get('CODEQL_BIN', '/opt/checkerbench/codeql-2.26.4/codeql')


def restore_rv_monitor_config(worktree):
    """Restore a missing generated integer from this tree's config evidence."""
    header = worktree/'include/generated/autoconf.h'
    kconfig = worktree/'kernel/trace/rv/Kconfig'
    if not header.is_file() or not kconfig.is_file():
        return None
    text = header.read_text()
    symbol = 'CONFIG_RV_PER_TASK_MONITORS'
    if not re.search(r'^#define CONFIG_RV 1$', text, re.M) or re.search(r'^#define '+symbol+r'\b', text, re.M):
        return None
    source = kconfig.read_text()
    block = re.search(r'(?ms)^config RV_PER_TASK_MONITORS\n(.*?)(?=^(?:config|menuconfig|source|endif|endmenu)\b|\Z)', source)
    if not block or not re.search(r'^[ \t]+depends on RV[ \t]*$', block[1], re.M):
        return None
    bounds = re.findall(r'^[ \t]+range ([0-9]+) ([0-9]+)[ \t]*$', block[1], re.M)
    if not re.search(r'^[ \t]+int(?:[ \t]|$)', block[1], re.M) or len(bounds) != 1:
        return None
    config_path = worktree/'.config'
    config = config_path.read_text() if config_path.is_file() else ''
    explicit = re.search(r'^'+symbol+r'=([^\n]+)$', config, re.M)
    if explicit:
        value_text = explicit[1].strip()
        origin = '.config'
    else:
        defaults = re.findall(r'^[ \t]+default ([^\n]+)$', block[1], re.M)
        if len(defaults) != 1:
            return None
        value_text = defaults[0].strip()
        origin = 'kernel/trace/rv/Kconfig unconditional default'
    if not re.fullmatch(r'[0-9]+', value_text):
        return None
    value = int(value_text)
    if not int(bounds[0][0]) <= value <= int(bounds[0][1]):
        raise RuntimeError('RV monitor configuration outside source Kconfig range')
    updated = text + '\n/* checkerbench replay: restored from this source tree configuration. */\n#define '+symbol+' '+str(value)+'\n'
    header.write_text(updated)
    return dict(kind='restore_rv_monitor_config', worktree=worktree.name, symbol=symbol,
                value=value, value_origin=origin, original_sha256=hashlib.sha256(text.encode()).hexdigest(),
                repaired_sha256=hashlib.sha256(updated.encode()).hexdigest(),
                kconfig_sha256=hashlib.sha256(source.encode()).hexdigest(),
                config_sha256=hashlib.sha256(config.encode()).hexdigest())

def restore_linux_anonymous_struct_flags(output):
    """Restore the required C extension for captured tagged anonymous structs."""
    env_path = output/'scan/envs.json'
    if not env_path.is_file():
        return []
    original_env = env_path.read_text()
    envs = json.loads(original_env)
    required = {}
    for entries in envs.values():
        for entry in entries.values():
            name = Path(entry.get('worktree_path') or entry.get('work_dir') or '').name
            wt = output/'scan/tmp'/name
            makefile = wt/'Makefile'
            header = wt/'include/linux/ns/ns_common_types.h'
            if not makefile.is_file() or not header.is_file():
                continue
            make_text, header_text = makefile.read_text(), header.read_text()
            if (re.search(r'^KBUILD_CFLAGS[ \t]*\+=[ \t]*-fms-extensions[ \t]*$', make_text, re.M)
                    and re.search(r'^[ \t]*struct ns_tree;[ \t]*$', header_text, re.M)):
                required[name] = dict(worktree=name, makefile_sha256=hashlib.sha256(make_text.encode()).hexdigest(),
                                      header_sha256=hashlib.sha256(header_text.encode()).hexdigest())
    if not required:
        return []
    changes = {}
    env_changed = False
    for entries in envs.values():
        for entry in entries.values():
            name = Path(entry.get('worktree_path') or entry.get('work_dir') or '').name
            if name in required and '-fms-extensions' not in shlex.split(entry['compile_flags']):
                entry['compile_flags'] = '-fms-extensions ' + entry['compile_flags']
                env_changed = True
    if env_changed:
        changes[env_path] = (original_env, json.dumps(envs, indent=2)+'\n')
    for path in (output/'scan').glob('*.sh'):
        old = path.read_text()
        lines = []
        for line in old.splitlines(keepends=True):
            if any(name in line for name in required) and '/opt/llvm/bin/clang --analyze' in line and '-fms-extensions' not in line:
                line = line.replace('/opt/llvm/bin/clang --analyze', '/opt/llvm/bin/clang -fms-extensions --analyze')
            lines.append(line)
        new = ''.join(lines)
        if old != new:
            changes[path] = (old, new)
    for path in (output/'scan').glob('flags_*.txt'):
        old = path.read_text()
        if any(name in old for name in required) and '-fms-extensions' not in shlex.split(old):
            changes[path] = (old, '-fms-extensions '+old)
    if not changes:
        return []
    files = []
    for path, (old, new) in changes.items():
        backup = output/'runtime-originals/required-linux-build-flags'/path.name
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            backup.write_text(old)
        path.write_text(new)
        files.append(dict(path=str(path.relative_to(output)), original_sha256=hashlib.sha256(old.encode()).hexdigest(),
                          repaired_sha256=hashlib.sha256(new.encode()).hexdigest(), backup=str(backup.relative_to(output))))
    return [dict(kind='restore_required_linux_build_flag', flag='-fms-extensions',
                 reason='Source Makefile requires this flag for tagged anonymous structs used in ns_common.',
                 source_evidence=list(required.values()), files=files)]


def repair_csa_runtime(output, project_id):
    """Apply evidenced compatibility fixes only to writable replay assets."""
    adjustments = []
    if project_id == 'openexr_openexr':
        gcc_dir = '/usr/lib/gcc/x86_64-linux-gnu/11'
        if not Path(gcc_dir).is_dir(): raise RuntimeError('GCC 11 development headers are required for OpenEXR replay')
        for p in (output/'scan').glob('scan_*.sh'):
            text = p.read_text()
            updated = text.replace('/opt/llvm/bin/clang --analyze', f'/opt/llvm/bin/clang --gcc-install-dir={gcc_dir} --analyze')
            if updated != text: p.write_text(updated)
        adjustments.append({'kind':'gcc_header_version','project':project_id,'gcc_install_dir':gcc_dir,'reason':'Old OpenEXR source requires the original GCC 11 C++ header transitive includes.'})
    for wt in (output/'scan/tmp').glob('wt_*'):
        kconfig = wt/'init/Kconfig'
        header = wt/'include/generated/autoconf.h'
        if not kconfig.is_file() or not header.is_file(): continue
        block = re.search(r'(?ms)^config MEMCG\n(.*?)(?=^(?:config|menuconfig|endmenu|endif)\b|\Z)', kconfig.read_text())
        text = header.read_text()
        if block and re.search(r'^\s+select SLAB_OBJ_EXT\s*$', block[1], re.M) and '#define CONFIG_MEMCG 1' in text and not re.search(r'^#define CONFIG_SLAB_OBJ_EXT\b',text,re.M):
            updated = text + '\n/* checkerbench replay: selected by MEMCG in this source tree Kconfig. */\n#define CONFIG_SLAB_OBJ_EXT 1\n'
            header.write_text(updated)
            adjustments.append({'kind':'restore_selected_kconfig_symbol','worktree':wt.name,'symbol':'CONFIG_SLAB_OBJ_EXT','value':1,'reason':'Source init/Kconfig: MEMCG selects SLAB_OBJ_EXT; exported autoconf omitted this required symbol.','original_sha256':hashlib.sha256(text.encode()).hexdigest(),'repaired_sha256':hashlib.sha256(updated.encode()).hexdigest()})
        rv_adjustment = restore_rv_monitor_config(wt)
        if rv_adjustment:
            adjustments.append(rv_adjustment)
    if project_id == 'torvalds_linux':
        adjustments.extend(restore_linux_anonymous_struct_flags(output))
    (output/'runtime-adjustments.json').write_text(json.dumps(adjustments,indent=2))
    return adjustments


def prepare_csa(task_id, output, reference=False):
    pair_id = int(task_id.removeprefix('pair_'))
    task_id = f'pair_{pair_id}'
    index = json.loads((CSA / 'runtime/index.json').read_text())
    info = index['pairs'][str(pair_id)]
    selected = next(json.loads(line) for line in (CSA/'selected_results.jsonl').read_text().splitlines() if json.loads(line)['pair_id'] == pair_id)
    task_result = json.loads((CSA/'references'/task_id/'task_result.json').read_text())
    subprocess.run(['python3', str(CSA/'scripts/rehydrate_scan_env.py'), '--pair-id', str(pair_id), '--output-workdir', str(output)], check=True)
    replacements = {task_result['success_workdir']: str(output)}
    for side, side_info in info['sides'].items():
        wt = output/'scan/tmp'/side_info['worktree_basename']
        snap = output/'.benchmark-runtime/snapshots'/side_info['snapshot_key']
        replacements[side_info['original_worktree_path']] = str(wt)
        replacements[side_info['original_snapshot_path']] = str(snap)
    files = list((output/'scan').glob('*.sh')) + list((output/'scan').glob('*.txt')) + [output/'scan/envs.json', output/'config.json']
    files += list((output/'.benchmark-runtime/snapshots').glob('*/compile_commands.json'))
    for path in files:
        if not path.is_file(): continue
        text = path.read_text(errors='replace')
        for old, new in sorted(replacements.items(), key=lambda item: -len(item[0])):
            text = text.replace(old, new)
        path.write_text(text)
    config_path = output/'config.json'
    config = json.loads(config_path.read_text())
    config['repo_path'] = str(output/'scan/tmp'/info['sides']['before']['worktree_basename'])
    config['llvm_build_dir'] = '/opt/llvm'
    config_path.write_text(json.dumps(config, indent=2, ensure_ascii=False))
    repair_csa_runtime(output, config['project_id'])
    (output/'.mcp.json').write_text(json.dumps({'mcpServers': {'csa-tools': {'type':'stdio', 'command':'/opt/checkerbench/venv/bin/python', 'args':['/opt/checkerbench/mcp-server.py'], 'env':{'LLVM_BUILD_DIR':'/opt/llvm','CHECKERBENCH_WORK_ROOT':str(output)}}}}, indent=2))
    if reference:
        shutil.copy2(CSA/selected['reference_success_workdir']/'checker.cpp', output/'checker.cpp')
    (output/'checkerbench-workspace.json').write_text(json.dumps({'backend':'csa','task_id':task_id,'reference_loaded':reference,'llvm_version':'18.1.8'}, indent=2))
    return output


def prepare_codeql(language, task_id, output, reference=False):
    roots = [p for p in ROOT.glob(f'codeql_{language}_*') if p.is_dir()]
    if len(roots) != 1: raise RuntimeError(f'Expected one benchmark for {language}')
    root = roots[0]
    item = next(t for t in json.loads((root/'manifest.json').read_text())['tasks'] if t['dataset_item_id'] == task_id)
    output.mkdir(parents=True, exist_ok=False)
    task_dir = output/'tasks'/task_id
    shutil.copytree(root/'tasks'/task_id, task_dir)
    # Exported API-search helpers reference the original machine's bundle path.
    for script in (task_dir/'scripts').glob('*.sh'):
        text = script.read_text()
        text = text.replace('/workspace/checkerGym/codeql-tools/codeql-bundle-v2.26.4', str(Path(CODEQL).parent))
        script.write_text(text)
    # Databases are copied: query caches must not modify the exported originals.
    for asset in item['assets'].values():
        relative = Path(asset['database'])
        dest = output/relative
        if not dest.exists():
            dest.parent.mkdir(parents=True,exist_ok=True)
            subprocess.run(['cp','-a','--reflink=auto',str(root/relative),str(dest)],check=True)
    if reference:
        shutil.copytree(root/item['reference']/'query', task_dir/'query', dirs_exist_ok=True)
    (output/'env.sh').write_text(f'export CODEQL_BIN={CODEQL}\nexport LLVM_BUILD_DIR=/opt/llvm\n')
    (output/'checkerbench-workspace.json').write_text(json.dumps({'backend':'codeql','language':language,'task_id':task_id,'reference_loaded':reference,'codeql_version':'2.26.4'},indent=2))
    return task_dir


def main():
    global ROOT, CSA
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backend',choices=['csa','codeql'])
    parser.add_argument('task_id')
    parser.add_argument('--language',choices=['go','java','javascript','python'])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--data-root', type=Path, default=ROOT)
    parser.add_argument('--roster', type=Path, default=Path(__file__).resolve().parents[1]/'dataset/tasks.jsonl')
    parser.add_argument('--reference',action='store_true',help='Maintainer validation only: copy the successful reference checker into the workspace.')
    args=parser.parse_args();out=args.output.expanduser().resolve()
    ROOT=args.data_root.expanduser().resolve();CSA=ROOT/'csa159'
    rows=[json.loads(line) for line in args.roster.read_text().splitlines() if line.strip()]
    matches=[r for r in rows if r['backend']==args.backend and r['task_id']==args.task_id
             and (args.backend=='csa' or r['language']==args.language)]
    if len(matches)!=1:parser.error('Task is not in the fixed 300-task roster')
    if out.exists():parser.error(f'Output already exists: {out}')
    if args.backend=='codeql' and not args.language:parser.error('--language is required for codeql')
    out.parent.mkdir(parents=True,exist_ok=True)
    if args.backend=='csa':dest=prepare_csa(args.task_id,out,args.reference)
    else:dest=prepare_codeql(args.language,args.task_id,out,args.reference)
    print(f'WORKSPACE={dest}')


if __name__=='__main__':main()
