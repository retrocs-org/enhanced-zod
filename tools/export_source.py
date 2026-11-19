#!/usr/bin/env python3
"""Create a reviewable source-only Git repository without inherited media history."""
import argparse,importlib.util,json,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import release

def export(root,destination):
    root=Path(root).resolve();destination=Path(destination).resolve()
    if root/'outputs' not in destination.parents:raise ValueError('Export must be under the checkout outputs directory')
    files=release.sources(root)
    if destination.exists():raise ValueError('Choose a fresh export directory; existing exports are not overwritten')
    destination.mkdir(parents=True)
    for name in files:
        target=destination/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/name,target)
    subprocess.run(['git','init','-q','--initial-branch=main',str(destination)],check=True)
    for key in ['user.name','user.email']:
        value=release.git(root,'config',key)
        subprocess.run(['git','-C',str(destination),'config',key,value],check=True)
    config=release.configured(root/'examples/release-config.json') if (root/'examples/release-config.json').exists() else {}
    for service,name in [('gitlab','origin'),('github','github')]:
        if config.get(service) or config.get(service+'_repo'):
            subprocess.run(['git','-C',str(destination),'remote','add',name,release.destination(config,service)['push_url']],check=True)
    subprocess.run(['git','-C',str(destination),'config','core.hooksPath','.githooks'],check=True)
    subprocess.run(['git','-C',str(destination),'add','--all'],check=True)
    subprocess.run(['git','-C',str(destination),'commit','-qm','Import zod-ee source without original game media'],check=True)
    print('Source-only export:',destination)
    print('Review and push with: python3 '+str(destination/'scripts/release.py')+' source --to gitlab,github --dry-run')
    return destination
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    try:export(ROOT,args.output)
    except (ValueError,OSError,subprocess.CalledProcessError,release.ReleaseError):sys.exit('Export failed; check source policy, Git identity, and destination.')
