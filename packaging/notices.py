#!/usr/bin/env python3
"""Collect local dependency license texts and a hashed CycloneDX file inventory."""
import argparse,hashlib,json,os,platform,re,shutil,subprocess
from pathlib import Path

def inventory(package, destination, sources=()):
    package=Path(package).resolve();destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    licenses=destination/'licenses';licenses.mkdir(exist_ok=True)
    roots=set();components=[];texts=set();origins=[]
    for raw in sources:
        path=Path(raw).resolve()
        if 'Cellar' in path.parts:
            i=path.parts.index('Cellar');roots.add(Path(*path.parts[:i+3]))
        if platform.system()=='Windows':roots.add(path.parent.parent/'share/licenses')
        if platform.system()=='Linux' and path.is_file():
            result=subprocess.run(['dpkg-query','-S',str(path)],text=True,capture_output=True)
            for line in result.stdout.splitlines():
                pkg=line.split(': ')[0].split(':')[0];copyright=Path('/usr/share/doc')/pkg/'copyright'
                if copyright.is_file():texts.add(copyright)
        origins.append(path.name)
    for root in roots:
        if not root.is_dir():continue
        for path in root.rglob('*'):
            if path.is_file() and (path.name.lower().startswith(('license','copying','copyright','notice')) or 'share/licenses' in path.as_posix()):
                if path.stat().st_size<=2*1024*1024:texts.add(path)
    for source in sorted(texts):
        data=source.read_bytes();key=hashlib.sha256(data).hexdigest()[:12];target=licenses/(key+'-'+source.name)
        if not target.exists():target.write_bytes(data)
    for path in sorted(package.rglob('*')):
        if not path.is_file() or path.is_symlink() or destination.resolve() in path.parents:continue
        # The app executable is sealed after this resource inventory; its final hash is in release provenance.
        if package.suffix=='.app' and path==package/'Contents/MacOS/zod-ee':continue
        if path.suffix.lower() not in {'.dylib','.dll','.exe','.so'} and '.so.' not in path.name and not os.access(path,os.X_OK):continue
        components.append({'type':'file','name':path.relative_to(package).as_posix(),'hashes':[{'alg':'SHA-256','content':hashlib.sha256(path.read_bytes()).hexdigest()}]})
    (destination/'bom.cdx.json').write_text(json.dumps({'bomFormat':'CycloneDX','specVersion':'1.5','version':1,'metadata':{'component':{'type':'application','name':'zod-ee'}},'components':components},indent=2)+'\n')
    (destination/'THIRD-PARTY-NOTICES.txt').write_text('zod-ee bundled dependencies\n\nzod-ee is GPL-3.0; see the root LICENSE. Bundled libraries and media tools\nretain their own licenses. The final macOS main executable hash is recorded\nin release provenance because app sealing occurs after this inventory. License texts collected from the native build\ninstallation are in licenses/. bom.cdx.json inventories actual bundled\nexecutables/libraries with SHA-256 hashes. System libraries are not bundled.\nThe collected license corpus can include transitive build dependencies.\n\nBuild dependency files:\n'+''.join('  '+name+'\n' for name in sorted(set(origins)))+'\nReview the inventory and matching source offers before public distribution.\n',encoding='utf-8')
    if not texts:raise RuntimeError('No dependency license texts found; provide native dependency source paths')
    return len(components),len(texts)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--package',type=Path,required=True);parser.add_argument('--destination',type=Path,required=True);parser.add_argument('--source',action='append',default=[]);parser.add_argument('--source-list',type=Path)
    args=parser.parse_args();sources=args.source
    if args.source_list:sources += [line.split('|')[-1] for line in args.source_list.read_text().splitlines() if line.strip()]
    print('Inventory files / license texts:',inventory(args.package,args.destination,sources))
