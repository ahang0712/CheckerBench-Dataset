#!/usr/bin/env python3
"""Verify split archives, extract datasets, and optionally import the Docker image."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile

from download import valid

class Parts(io.RawIOBase):
    def __init__(self,paths):
        self.paths=iter(paths);self.current=None
    def readable(self):return True
    def readinto(self,b):
        while True:
            if self.current is None:
                try:self.current=next(self.paths).open('rb')
                except StopIteration:return 0
            n=self.current.readinto(b)
            if n:return n
            self.current.close();self.current=None
    def close(self):
        if self.current:self.current.close()
        super().close()

def path_within(base,name,root):
    name=PurePosixPath(name)
    if name.is_absolute() or '..' in name.parts or not name.parts or name.parts[0]!=root:
        raise ValueError('Unexpected archive path: '+str(name))
    path=base/str(name)
    if not path.parent.resolve().is_relative_to(base.resolve()):raise ValueError('External archive parent')
    return path

def extract(stream,base,root):
    links=[];directories=[]
    with tarfile.open(fileobj=stream,mode='r|gz') as tar:
        for member in tar:
            target=path_within(base,member.name,root)
            target.parent.mkdir(parents=True,exist_ok=True)
            if member.issym() or member.islnk():links.append(member);continue
            if member.isdir():target.mkdir(exist_ok=True);directories.append((target,member.mode));continue
            if not member.isfile():raise ValueError('Special file in archive: '+member.name)
            with tar.extractfile(member) as src,target.open('xb') as dst:shutil.copyfileobj(src,dst,8*1024**2)
            target.chmod(member.mode & 0o777)
    # Historical absolute symlinks are preserved as data, after all file writes.
    for member in links:
        target=path_within(base,member.name,root)
        if member.issym():target.symlink_to(member.linkname)
        else:
            source=path_within(base,member.linkname,root)
            if source.is_symlink() or not source.is_file():raise ValueError('Invalid hard link')
            os.link(source,target)
    for target,mode in reversed(directories):target.chmod(mode & 0o777)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',type=Path,default=Path('downloads'))
    p.add_argument('--data',type=Path,default=Path('data'))
    p.add_argument('--manifest',type=Path,default=Path(__file__).resolve().parents[1]/'release-manifest.json')
    p.add_argument('--load-image',action='store_true')
    p.add_argument('--image-only',action='store_true')
    a=p.parse_args();manifest=json.loads(a.manifest.read_text());a.data.mkdir(parents=True,exist_ok=True)
    for package in manifest['packages']:
        if package['kind']=='docker-image' and not (a.load_image or a.image_only):continue
        if package['kind']=='dataset' and a.image_only:continue
        paths=[a.assets/x['name'] for x in package['parts']]
        full=hashlib.sha256()
        for path,part in zip(paths,package['parts']):
            if not valid(path,part):raise SystemExit('Missing or corrupt asset: '+str(path))
            with path.open('rb') as f:
                for b in iter(lambda:f.read(8*1024**2),b''):full.update(b)
        if full.hexdigest()!=package['sha256']:raise SystemExit('Archive checksum mismatch: '+package['archive'])
        with io.BufferedReader(Parts(paths),buffer_size=8*1024**2) as stream:
            if package['kind']=='docker-image':
                process=subprocess.Popen(['docker','load'],stdin=subprocess.PIPE)
                shutil.copyfileobj(stream,process.stdin,8*1024**2);process.stdin.close()
                if process.wait():raise SystemExit('docker load failed')
            else:
                receipt=a.data/(package['root']+'.restored.json')
                if receipt.exists() and json.loads(receipt.read_text())['sha256']==package['sha256']:
                    print('already restored '+package['root']);continue
                if (a.data/package['root']).exists():raise SystemExit('Use a fresh data directory or move incomplete extraction: '+package['root'])
                extract(stream,a.data,package['root'])
                receipt.write_text(json.dumps(dict(sha256=package['sha256'],task_count=package['task_count']))+'\n')
            print('restored '+package['key'],flush=True)

if __name__=='__main__':main()
