#!/usr/bin/env python3
"""Download release assets through gh, checking SHA-256 and reusing valid files."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.parse
import urllib.request
import shutil

def sha256(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(8*1024**2),b''):h.update(block)
    return h.hexdigest()

def valid(path, entry):
    return path.is_file() and path.stat().st_size==entry['bytes'] and sha256(path)==entry['sha256']

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=Path('downloads'))
    p.add_argument('--manifest',type=Path,default=Path(__file__).resolve().parents[1]/'release-manifest.json')
    p.add_argument('--repo', help='GitHub owner/repository; defaults to the current checkout origin')
    p.add_argument('--base-url', help='Anonymous download endpoint serving release asset filenames')
    p.add_argument('--kind',choices=['all','dataset','docker-image'],default='all')
    a=p.parse_args();manifest=json.loads(a.manifest.read_text());a.output.mkdir(parents=True,exist_ok=True)
    repository=a.repo
    if not a.base_url and not repository:
        repository=subprocess.check_output(['gh','repo','view','--json','nameWithOwner','--jq','.nameWithOwner'],text=True).strip()
    for package in manifest['packages']:
        if a.kind!='all' and package['kind']!=a.kind:continue
        for part in package['parts']:
            target=a.output/part['name']
            if not valid(target,part):
                if a.base_url:
                    url=a.base_url.rstrip('/')+'/'+urllib.parse.quote(part['name'])
                    temporary=target.with_suffix(target.suffix+'.partial')
                    with urllib.request.urlopen(url) as src,temporary.open('wb') as dst:
                        shutil.copyfileobj(src,dst,8*1024**2)
                    temporary.replace(target)
                else:
                    subprocess.run(['gh','release','download',manifest['tag'],'--repo',repository,
                                    '--pattern',part['name'],'--dir',str(a.output),'--clobber'],check=True)
                if not valid(target,part):raise SystemExit('Checksum mismatch: '+part['name'])
            print('verified '+part['name'],flush=True)

if __name__=='__main__':main()
