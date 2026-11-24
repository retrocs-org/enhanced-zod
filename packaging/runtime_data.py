"""Copy only version-controlled/runtime source data, excluding imported media."""
from pathlib import Path
import shutil
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import source_policy

def copy_runtime(root, destination, website=False):
    root=Path(root);destination=Path(destination)
    omitted=set()
    for name in ('imported-assets.txt','obsolete-assets.txt'):
        file=root/'packaging'/name
        if file.exists():omitted.update('assets/'+line.strip() for line in file.read_text().splitlines() if line.strip() and not line.startswith('#'))
    files=subprocess.check_output(['git','-C',str(root),'ls-files','-z','--cached','--others','--exclude-standard']).decode().split('\0')
    for name in sorted(set(filter(None,files))):
        relative=Path(name);source=root/relative
        if not source.is_file():continue
        selected=name.startswith(('assets/','maps/')) or name in {'map_list.txt','default_settings.txt','README.md','LICENSE','VERSION'}
        if website and name.startswith('public-web/'):selected=True
        if source_policy.forbidden(name):continue
        if not selected or name in omitted or name.startswith(('assets/videos/','assets/original-z/')):continue
        if source.is_symlink() or any((root/p).is_symlink() for p in relative.parents):raise RuntimeError('Linked runtime source: '+name)
        if relative.name in {'.env','config.local.php'} or relative.suffix in {'.pem','.key','.p12'}:raise RuntimeError('Private runtime configuration: '+name)
        target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)

    # Licensed font dependencies are downloaded locally rather than stored in Git.
    for folder in ('assets/frontend/fonts','assets/soundfonts'):
        source=root/folder
        if source.is_dir():
            for file in source.iterdir():
                if file.suffix.lower() in {'.ttf','.sf2','.sf3'} and file.is_file():
                    target=destination/folder/file.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
    if website and (root/'public-web/assets/heading.ttf').is_file():
        target=destination/'public-web/assets/heading.ttf';target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/'public-web/assets/heading.ttf',target)
