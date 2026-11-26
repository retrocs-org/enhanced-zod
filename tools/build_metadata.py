#!/usr/bin/env python3
"""Deterministic source build identity, refreshed before every native build."""
import argparse,hashlib,json,platform,plistlib,subprocess
from pathlib import Path

def metadata(root):
 def git(*args):
  try:return subprocess.check_output(['git','-C',str(root),*args],stderr=subprocess.DEVNULL).decode().strip()
  except (OSError,subprocess.CalledProcessError):return ''
 commit=git('rev-parse','HEAD')
 if not commit:
  try:commit=json.loads((root/'BUILD-PROVENANCE.json').read_text()).get('commit','')
  except (OSError,ValueError):pass
 commit=commit if isinstance(commit,str) and len(commit)==40 and all(c in '0123456789abcdef' for c in commit) else 'unversioned'
 count=git('rev-list','--count','HEAD') or '0'
 files=sorted([p for p in (root/'src').rglob('*') if p.is_file() and p.suffix in {'.h','.cpp','.c','.hpp'}]+[root/'CMakeLists.txt',root/'VERSION',root/'tools/build_metadata.py'])
 digest=hashlib.sha256()
 for p in files:
  digest.update(p.relative_to(root).as_posix().encode()+b'\0'+p.read_bytes()+b'\0')
 fingerprint=digest.hexdigest();dirty=bool(git('status','--porcelain','--untracked-files=normal','--','src','CMakeLists.txt','VERSION','tools/build_metadata.py'))
 version=(root/'VERSION').read_text().strip()
 return {'version':version,'commit':commit,'source_sha256':fingerprint,'dirty':dirty,'build_id':version+'+'+commit[:12]+'.'+fingerprint[:12]+('-dirty' if dirty else ''),'build_number':count,'platform':platform.system()+'-'+platform.machine()}

def write_changed(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--plist',type=Path);parser.add_argument('--stamp-only',action='store_true');parser.add_argument('--config',default='unspecified');parser.add_argument('--compiler',default='unspecified');parser.add_argument('--platform');parser.add_argument('--version');args=parser.parse_args()
 if args.stamp_only:
  if not args.plist:parser.error('--stamp-only requires --plist')
  m=json.loads((args.output/'BUILD-IDENTITY.json').read_text())
 else:
  m=metadata(args.root.resolve());m['configuration']=args.config;m['compiler']=args.compiler
  if args.platform:m['platform']=args.platform
  if args.version:m['version']=args.version
  m['build_id']=m['version']+'+'+m['commit'][:12]+'.'+m['source_sha256'][:12]+('-dirty' if m['dirty'] else '')+'.'+args.config
  write_changed(args.output/'BUILD-IDENTITY.json',(json.dumps(m,indent=2,sort_keys=True)+'\n').encode())
  header='#pragma once\n#define ZOD_BUILD_ID_LITERAL '+json.dumps(m['build_id'])+'\n#define ZOD_BUILD_JSON_LITERAL '+json.dumps(json.dumps(m,sort_keys=True))+'\n'
  write_changed(args.output/'zbuild_metadata.h',header.encode())
 if args.plist:
  with args.plist.open('rb') as f:p=plistlib.load(f)
  p['CFBundleVersion']=m['build_number'];p['CFBundleGetInfoString']='zod-enhanced '+m['build_id'];p['ZodBuildID']=m['build_id'];write_changed(args.plist,plistlib.dumps(p))
if __name__=='__main__':main()
