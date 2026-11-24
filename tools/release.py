#!/usr/bin/env python3
"""zod-enhanced release preparation, provenance, verification and explicit publication.

Adapted to this project's platforms from the staged workflow demonstrated by
Beam68. No source push, tag creation or publication happens implicitly.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import tarfile
import zipfile
from urllib.parse import urlsplit
sys.path.insert(0,str(Path(__file__).resolve().parent))
import release_credentials as credentials
import source_policy

ROOT = Path(__file__).resolve().parents[1]
SKIP = {'.git', '.codex', '.agents', '.aws', 'outputs', 'dist', 'work', '__pycache__', '.pytest_cache', 'node_modules', '.venv'}
PRIVATE = {'.env', '.netrc', '.npmrc', '.pypirc', '.git-credentials', 'id_rsa', 'id_ed25519', 'credentials', 'release-config.json', '.my.cnf'}
class ReleaseError(RuntimeError): pass

def need(condition, message):
    if not condition: raise ReleaseError(message)

def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''): h.update(chunk)
    return h.hexdigest()

def encoded(value): return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()
def fingerprint(files): return hashlib.sha256(encoded(files)).hexdigest()
def atomic(path, data):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    need(not path.is_symlink(), 'Linked output metadata is not supported')
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name); stream.write(data); stream.flush(); os.fsync(stream.fileno())
    try: os.replace(temporary, path)
    finally: temporary.unlink(missing_ok=True)

def git(root, *args): return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.DEVNULL).decode().strip()
def version(root):
    value = (root/'VERSION').read_text().strip()
    need(re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9][A-Za-z0-9.-]*)?', value), 'VERSION must be a semantic version')
    return value

def private(path):
    return (path.name in PRIVATE and path.as_posix() != 'examples/release-config.json') or path.suffix.lower() in {'.pem','.key','.p12','.pfx'} or (path.name.startswith('.env') and path.name not in {'.env.example','.env.sample'})

def sources(root):
    listing = subprocess.check_output(['git','-C',str(root),'ls-files','-z','--cached','--others','--exclude-standard']).decode().split('\0')
    files = {}
    for name in sorted(set(filter(None, listing))):
        relative=Path(name)
        if source_policy.forbidden(name): continue
        if any(part in SKIP for part in relative.parts) or any(part.startswith('build-') or part=='build' for part in relative.parts[:-1]): continue
        if relative.name in {'.gitkeep','.DS_Store','BUILD-PROVENANCE.json','RELEASE-BUILD.json'} or relative.suffix.lower() in {'.pyc','.log','.iso','.cue','.bin'}: continue
        if name.startswith(('assets/original-z/','assets/videos/')) and relative.name!='README.md': raise ReleaseError('Imported game media cannot be released: '+name)
        if private(relative): raise ReleaseError('Private/local configuration cannot be packaged: '+name)
        path=root/relative
        if not path.exists(): continue # tracked deletions in an explicitly dirty preview
        need(not path.is_symlink() and all(not (root/p).is_symlink() for p in relative.parents), 'Linked source path: '+name)
        if path.is_file():
            need(not credentials.contains_secret(path.read_bytes()), "Credential found in source input: "+name)
            files[name]=digest(path)
    need('VERSION' in files and 'README.md' in files and 'LICENSE' in files, 'Source tree is incomplete')
    return files

def source_record(root):
    files=sources(root)
    return {'version':version(root),'commit':git(root,'rev-parse','HEAD'),'dirty':bool(git(root,'status','--porcelain')),'source_sha256':fingerprint(files),'files':files}

def record_build(root, build, binaries, tests=False):
    record=source_record(root);record['binaries']={}
    for binary in binaries:
        path=Path(binary).resolve();need(path.is_file(),'Built binary not found: '+str(path))
        record['binaries'][str(path.relative_to(root))]=digest(path)
    record['tests_passed']=bool(tests)
    atomic(build/'BUILD-PROVENANCE.json',encoded(record));return record

def output_dir(root, selected):
    path=Path(selected) if selected else root/'outputs'/('release-'+version(root))
    if not path.is_absolute(): path=root/path
    path=Path(os.path.abspath(path));base=root/'outputs'
    need(base in path.parents, 'Release output must be a child of this repository outputs/ directory')
    for item in [path,*path.parents]:
        need(not item.is_symlink(),'Linked release output is not supported')
        if item==root: break
    return path

def add_file(archive,path,name):
    info=zipfile.ZipInfo(name,(2020,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
    info.create_system=3;info.external_attr=(stat.S_IFREG|(path.stat().st_mode&0o777))<<16
    archive.writestr(info,path.read_bytes())

def packaged_hashes(path):
    hashes=set()
    if path.is_dir():
        return {digest(file) for file in path.rglob('*') if file.is_file() and not file.is_symlink()}
    if path.suffix=='.zip':
        with zipfile.ZipFile(path) as archive:
            for entry in archive.infolist():
                if not entry.is_dir(): hashes.add(hashlib.sha256(archive.read(entry)).hexdigest())
    elif path.name.endswith('.tar.gz'):
        with tarfile.open(path,'r:gz') as archive:
            for entry in archive:
                if entry.isfile():
                    stream=archive.extractfile(entry); h=hashlib.sha256()
                    for chunk in iter(lambda: stream.read(1024*1024),b''): h.update(chunk)
                    hashes.add(h.hexdigest())
    return hashes

def archive_tree(source, destination, prefix):
    source=source.resolve()
    with zipfile.ZipFile(destination,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for path in sorted(source.rglob('*')):
            relative=path.relative_to(source)
            need(not private(relative), 'Private file in runtime package: '+str(relative))
            if path.is_symlink():
                need(path.resolve().is_relative_to(source),'Runtime link escapes its package')
                info=zipfile.ZipInfo(prefix+'/'+relative.as_posix(),(2020,1,1,0,0,0));info.create_system=3;info.external_attr=(stat.S_IFLNK|0o777)<<16
                archive.writestr(info,os.readlink(path).encode())
            elif path.is_file(): add_file(archive,path,prefix+'/'+relative.as_posix())

def _prepare(root,out,artifacts,allow_dirty=False,notes=None):
    record=source_record(root);need(allow_dirty or not record['dirty'],'Commit or stash changes before release preparation, or use --allow-dirty for a local preview')
    need(not out.exists(),'Output already exists; verify it or select a new output directory')
    notes=Path(notes or root/'docs/RELEASE-NOTES.md');notes=notes if notes.is_absolute() else root/notes
    need(notes.is_file() and len(notes.read_text().strip())>30,'Add reviewable release notes first')
    v=record['version'];out.mkdir(parents=True)
    names=[]
    source=out/f'zod-enhanced-{v}-source.zip'
    with zipfile.ZipFile(source,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name in record['files']: add_file(archive,root/name,'zod-enhanced-'+v+'/'+name)
        archive.writestr('zod-enhanced-'+v+'/BUILD-PROVENANCE.json',encoded(record))
    names.append(source.name)
    website=out/f'zod-enhanced-{v}-web.zip'
    with zipfile.ZipFile(website,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        add_file(archive,root/'LICENSE','zod-enhanced-web/LICENSE')
        for name in record['files']:
            if name.startswith('public-web/') or name == 'docs/NETWORK-SERVICES.md': add_file(archive,root/name,'zod-enhanced-web/'+name)
    names.append(website.name)
    platforms={}
    for label, raw in artifacts:
        need(re.fullmatch(r'(macos|windows|linux|linux-server)-(arm64|x86_64)',label),'Unsupported artifact label')
        path=Path(raw);path=path if path.is_absolute() else root/path
        need(path.exists() and not path.is_symlink(),'Runtime artifact missing or linked')
        if path.is_dir():
            name=f'zod-enhanced-{v}-{label}.zip';archive_tree(path,out/name,path.name)
        else:
            suffix='.tar.gz' if path.name.endswith('.tar.gz') else path.suffix
            need(suffix in {'.zip','.dmg','.tar.gz'},'Use a complete runtime package, not a bare executable')
            name=f'zod-enhanced-{v}-{label}{suffix}';shutil.copyfile(path,out/name)
        need(label not in platforms,'Duplicate runtime platform')
        platforms[label]=name;names.append(name)
    manifest={'schema':1,'project':'zod-enhanced','version':v,'tag':'v'+v,'source':record,'platforms':platforms,'artifacts':{},'runtime_provenance':{}}
    for label,raw in artifacts:
        artifact=Path(raw);artifact=artifact if artifact.is_absolute() else root/artifact
        evidence=None
        for parent in [artifact.parent,*artifact.parents]:
            candidate=parent/'BUILD-PROVENANCE.json'
            if candidate.is_file(): evidence=json.loads(candidate.read_text());break
            if parent==root: break
        if evidence is None:
            need(allow_dirty,'Runtime packages need build/check provenance; run check with --binary first')
            manifest['runtime_provenance'][label]={'verified':False,'tests_passed':False}
        else:
            need(evidence['source_sha256']==record['source_sha256'],'Build provenance is stale: rebuild/check from the prepared source tree')
            need(bool(evidence.get('binaries')),'Build evidence has no binaries')
            for name,sha in evidence['binaries'].items(): need((root/name).is_file() and digest(root/name)==sha,'Built binary changed after verification')
            if artifact.suffix!='.dmg':
                packaged=packaged_hashes(artifact)
                need(all(sha in packaged for sha in evidence['binaries'].values()),'Runtime package does not contain the verified binaries')
            manifest['runtime_provenance'][label]={'verified':True,'tests_passed':evidence.get('tests_passed',False),'binaries':evidence['binaries']}
    for name in names:
        path=out/name;manifest['artifacts'][name]={'sha256':digest(path),'size':path.stat().st_size}
        atomic(out/(name+'.sha256'),(digest(path)+'  '+name+'\n').encode())
    atomic(out/'SHA256SUMS',''.join(manifest['artifacts'][n]['sha256']+'  '+n+'\n' for n in sorted(names)).encode())
    atomic(out/'RELEASE-NOTES.md',notes.read_bytes())
    manifest['notes_sha256']=digest(out/'RELEASE-NOTES.md')
    atomic(out/'release-manifest.json',encoded(manifest))
    atomic(out/'downloads.json',encoded(downloads(manifest)))
    verify(root,out,False);return manifest

def prepare(root,out,artifacts,allow_dirty=False,notes=None):
    need(not out.exists(),'Output already exists; select a fresh release output directory')
    out.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.prepare-',dir=out.parent) as temporary:
        stage=Path(temporary)/'package'
        manifest=_prepare(root,stage,artifacts,allow_dirty,notes)
        os.replace(stage,out)
    return manifest

def collect(root,out,inputs):
    need(inputs and not out.exists(),'Provide prepared inputs and a fresh output directory')
    manifests=[verify(root,Path(folder)) for folder in inputs]
    first=manifests[0]
    need(all(m['source']==first['source'] and m['notes_sha256']==first['notes_sha256'] for m in manifests),'Platform packages must come from the exact same source snapshot and release notes')
    merged=json.loads(json.dumps(first));merged['artifacts']={};merged['platforms']={};merged['runtime_provenance']={}
    out.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.collect-',dir=out.parent) as temporary:
        stage=Path(temporary)/'package';stage.mkdir()
        for folder,manifest in zip(inputs,manifests):
            for name,entry in manifest['artifacts'].items():
                if name in merged['artifacts']: need(merged['artifacts'][name]==entry,'Conflicting artifact names')
                else:
                    merged['artifacts'][name]=entry;shutil.copyfile(Path(folder)/name,stage/name);shutil.copyfile(Path(folder)/(name+'.sha256'),stage/(name+'.sha256'))
            for label,name in manifest['platforms'].items():
                need(label not in merged['platforms'],'Duplicate runtime platform');merged['platforms'][label]=name
                merged['runtime_provenance'][label]=manifest['runtime_provenance'][label]
        shutil.copyfile(Path(inputs[0])/'RELEASE-NOTES.md',stage/'RELEASE-NOTES.md')
        atomic(stage/'SHA256SUMS',''.join(merged['artifacts'][name]['sha256']+'  '+name+'\n' for name in sorted(merged['artifacts'])).encode())
        atomic(stage/'release-manifest.json',encoded(merged));atomic(stage/'downloads.json',encoded(downloads(merged)))
        verify(root,stage);os.replace(stage,out)
    return merged

def downloads(manifest):
    entries={}
    for label,name in manifest['platforms'].items():
        entry={'version':manifest['version'],'url':'/downloads/'+name,'sha256':manifest['artifacts'][name]['sha256'],'architecture':label.split('-')[-1]}
        entries[label]=entry;entries.setdefault(label.split('-')[0],entry)
    return entries

def verify(root,out,current=True):
    manifest=json.loads((out/'release-manifest.json').read_text());need(manifest.get('project') in {'zod-enhanced','zod-ee'} and manifest.get('schema')==1,'Unsupported release manifest')
    need(fingerprint(manifest['source']['files'])==manifest['source']['source_sha256'],'Source manifest is inconsistent')
    for name,expected in manifest['artifacts'].items():
        need(Path(name).name==name,'Unsafe artifact filename');path=out/name
        need(path.is_file() and not path.is_symlink() and digest(path)==expected['sha256'] and path.stat().st_size==expected['size'],'Artifact failed verification: '+name)
        need((out/(name+'.sha256')).read_text()==expected['sha256']+'  '+name+'\n','Checksum sidecar changed: '+name)
        if name.endswith('.zip'):
            with zipfile.ZipFile(path) as archive: need(archive.testzip() is None,'ZIP integrity check failed')
    need(digest(out/'RELEASE-NOTES.md')==manifest['notes_sha256'],'Release notes changed after preparation')
    sums=''.join(manifest['artifacts'][n]['sha256']+'  '+n+'\n' for n in sorted(manifest['artifacts']))
    need((out/'SHA256SUMS').read_text()==sums,'SHA256SUMS changed')
    need(json.loads((out/'downloads.json').read_text())==downloads(manifest),'Download metadata changed after preparation')
    if current: need(source_record(root)['source_sha256']==manifest['source']['source_sha256'],'Prepared release no longer matches the working source tree')
    return manifest

def destination(config, service):
    value=config.get(service) or config.get(service+'_repo','')
    need(isinstance(value,str) and value, 'Configure the explicit '+service+' repository')
    if '://' not in value: value='https://'+('github.com/' if service=='github' else config.get('gitlab_host','gitlab.com')+'/')+value
    url=urlsplit(value)
    need(url.scheme=='https' and url.hostname and not url.username and not url.password and not url.query and not url.fragment and not url.port, 'Repository destinations must be credential-free HTTPS URLs')
    path=url.path.strip('/').removesuffix('.git')
    need(re.fullmatch(r'[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)+',path) and all(part not in {'.','..'} for part in path.split('/')), 'Invalid repository path')
    need(not (url.hostname.lower()=='github.com' and path.lower()=='fenio/zod'), 'The upstream repository is not a release destination')
    need(service!='github' or (url.hostname.lower()=='github.com' and path.count('/')==1), 'Use github.com/OWNER/REPOSITORY for GitHub')
    public='https://'+url.hostname+'/'+path
    push=config.get(service+'_git',public+'.git')
    need(push in {public+'.git',public,'git@'+url.hostname+':'+path+'.git','ssh://git@'+url.hostname+'/'+path+'.git'}, 'Git push URL must match its configured repository')
    return {'url':public,'push_url':push,'host':url.hostname,'path':path,'cli_repo':path if service=='github' else url.hostname+'/'+path}

def configured(path):
    data=json.loads(Path(path).read_text()) if path else {}
    need(isinstance(data,dict),'Release configuration must be an object')
    credentials.validate(data.get('credentials',{}))
    for service in ('github','gitlab'):
        if data.get(service) or data.get(service+'_repo'): destination(data,service)
    return data

def selected_targets(value):
    targets=value.split(',')
    need(targets and all(x in {'gitlab','github'} for x in targets) and len(set(targets))==len(targets), 'Choose distinct gitlab,github targets')
    return targets

def push_source(root,config,targets,confirmed=False,dry_run=False):
    need(dry_run or confirmed,'Source pushes require --confirm-push; use --dry-run first')
    need(Path(git(root,'rev-parse','--show-toplevel')).resolve()==root.resolve(),'Source must be the root of its own checkout')
    sha=git(root,'rev-parse','HEAD');need(not git(root,'status','--porcelain'),'Commit source changes before pushing')
    files=sources(root)
    need(not source_policy.history_paths(root), 'Inherited history contains game media. Create a clean source-only export with tools/export_source.py before pushing; deleting current files alone is insufficient.')
    for name in git(root,'ls-files','-z').split('\0'):
        if not name:continue
        relative=Path(name)
        need(not source_policy.forbidden(name), 'Imported media is tracked: '+name)
        need(not private(relative) and not any(part in SKIP for part in relative.parts) and not any(part.startswith('build-') or part=='build' for part in relative.parts[:-1]), 'Private/generated file is tracked: '+name)
    branch=config.get('source_branch','main');need(isinstance(branch,str) and re.fullmatch(r'[A-Za-z0-9_./-]+',branch) and not branch.startswith('-'),'Invalid source branch')
    subprocess.run(['git','check-ref-format','--branch',branch],check=True,capture_output=True)
    remotes={service:destination(config,service) for service in targets};refspec=sha+':refs/heads/'+branch
    for service,remote in remotes.items():
        print('Source preflight: '+service+' -> '+remote['url'],flush=True)
        subprocess.run(['git','-C',str(root),'push','--dry-run','--no-follow-tags','--',remote['push_url'],refspec],check=True)
    if dry_run:return {'commit':sha,'branch':branch,'uploaded':False,'targets':list(remotes)}
    for service,remote in remotes.items():
        need(git(root,'rev-parse','HEAD')==sha and not git(root,'status','--porcelain'),'Source changed before publication')
        subprocess.run(['git','-C',str(root),'push','--no-follow-tags','--',remote['push_url'],refspec],check=True)
        head=git(root,'ls-remote','--heads','--',remote['push_url'],'refs/heads/'+branch)
        need(head and head.split()[0]==sha,'Remote branch verification failed: '+service)
        atomic(root/'outputs/source'/('receipt-'+service+'.json'),encoded({'project':'zod-enhanced','commit':sha,'branch':branch,'repository':remote['url'],'verified':True}))
        print(service+': source branch verified',flush=True)
    return {'commit':sha,'branch':branch,'uploaded':True,'targets':list(remotes)}

def publish(root,out,config,target,confirmed,draft=True):
    need(confirmed,'Publication requires --confirm-publish')
    manifest=verify(root,out);need(not manifest['source']['dirty'] and not git(root,'status','--porcelain'),'Dirty previews cannot be published')
    tag=manifest['tag'];need(git(root,'rev-list','-n','1',tag)==manifest['source']['commit'],'Create a reviewed local version tag pointing at the prepared commit first')
    need(all(p['verified'] and p['tests_passed'] for p in manifest.get('runtime_provenance',{}).values()),'Run verified build checks for every runtime platform before publishing')
    remote=destination(config,target);repo=remote['cli_repo'];env=os.environ.copy()
    if target in config.get('credentials',{}):env['GH_TOKEN' if target=='github' else 'GITLAB_TOKEN']=credentials.token(target,config)
    if target=='gitlab':env['GITLAB_HOST']=remote['host']
    names=[*manifest['artifacts'],*(n+'.sha256' for n in manifest['artifacts']),'SHA256SUMS','release-manifest.json','RELEASE-NOTES.md','downloads.json']
    if target=='github':
        need(shutil.which('gh'),'Install/authenticate GitHub CLI for publication')
        subprocess.run(['gh','release','create',tag,'--repo',repo,'--target',manifest['source']['commit'],'--title','zod-enhanced '+manifest['version'],'--notes-file',str(out/'RELEASE-NOTES.md'),*(['--draft'] if draft else []),*[str(out/n) for n in names]],check=True,env=env)
    else:
        need(shutil.which('glab'),'Install/authenticate GitLab CLI for publication')
        subprocess.run(['glab','release','create',tag,'--repo',repo,'--ref',manifest['source']['commit'],'--name','zod-enhanced '+manifest['version'],'--notes-file',str(out/'RELEASE-NOTES.md'),*[str(out/n) for n in names]],check=True,env=env)
    atomic(out/('receipt-'+target+'.json'),encoded({'project':'zod-enhanced','version':manifest['version'],'repository':repo,'tag':tag,'manifest_sha256':digest(out/'release-manifest.json'),'draft':draft if target=='github' else False}))

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['plan','build','check','provenance','prepare','verify','publish','source','collect','status'])
    parser.add_argument('--input',type=Path,action='append',default=[]);parser.add_argument('--output',type=Path);parser.add_argument('--build-dir',type=Path,default=Path('build-app' if sys.platform=='darwin' else 'build'))
    parser.add_argument('--artifact',action='append',default=[],metavar='PLATFORM=PATH');parser.add_argument('--binary',action='append',default=[])
    parser.add_argument('--config',type=Path,default=ROOT/'examples/release-config.json');parser.add_argument('--to',help='Comma-separated gitlab,github destinations');parser.add_argument('--dry-run',action='store_true');parser.add_argument('--notes',type=Path);parser.add_argument('--allow-dirty',action='store_true')
    parser.add_argument('--target',choices=['github','gitlab'],default='github');parser.add_argument('--confirm-publish',action='store_true');parser.add_argument('--confirm-push',action='store_true');parser.add_argument('--public',action='store_true')
    args=parser.parse_args(argv);root=ROOT;config=configured(args.config);build=args.build_dir if args.build_dir.is_absolute() else root/args.build_dir;out=output_dir(root,args.output)
    if args.command=='plan':
        print(json.dumps({'project':'zod-enhanced','version':version(root),'build_dir':str(build),'output':str(out),'artifacts':args.artifact,'source_push':False,'publication':False,'targets':{k:destination(config,k)['url'] if config.get(k) or config.get(k+'_repo') else 'not configured' for k in ('gitlab','github')}},indent=2));return
    if args.command=='build':
        if sys.platform=='darwin':
            importer=root/'build-importer-venv/bin/python3'
            if not importer.exists():subprocess.run([sys.executable,'-m','venv',str(root/'build-importer-venv')],check=True)
            subprocess.run([str(importer),'-m','pip','install','-r',str(root/'packaging/importer/requirements.txt')],check=True)
        subprocess.run(['cmake','-S',str(root),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release',*(['-DZOD_MACOS_APP=ON'] if sys.platform=='darwin' else [])],check=True)
        subprocess.run(['cmake','--build',str(build),'--parallel','4',*(['--target','app'] if sys.platform=='darwin' else [])],check=True)
    elif args.command=='check':
        record_build(root,build,[str(Path(x) if Path(x).is_absolute() else root/x) for x in args.binary],False)
        subprocess.run(['ctest','--test-dir',str(build),'--output-on-failure'],check=True)
        subprocess.run([sys.executable,str(root/'tests/release_tool_test.py')],check=True)
        subprocess.run([sys.executable,str(root/'tests/package_tools_test.py')],check=True)
        record_build(root,build,[str(Path(x) if Path(x).is_absolute() else root/x) for x in args.binary],True)
    elif args.command=='provenance': record_build(root,build,[str(Path(x) if Path(x).is_absolute() else root/x) for x in args.binary])
    elif args.command=='prepare': prepare(root,out,[item.split('=',1) for item in args.artifact],args.allow_dirty,args.notes or config.get('notes'))
    elif args.command=='collect': collect(root,out,args.input)
    elif args.command in {'verify','status'}: verify(root,out)
    elif args.command=='publish':
        for target in selected_targets(args.to or args.target):publish(root,out,config,target,args.confirm_publish,not args.public)
    elif args.command=='source':
        result=push_source(root,config,selected_targets(args.to or 'gitlab,github'),args.confirm_push,args.dry_run);print(json.dumps(result,indent=2))
    print(args.command.capitalize()+(' dry run' if args.command=='source' and args.dry_run else '')+' completed'+('. No publication performed.' if args.command not in {'publish','source'} else '.'))

if __name__=='__main__':
    try: main()
    except (ReleaseError,OSError,ValueError,subprocess.CalledProcessError,KeyError) as error:
        # External tools handle their own interactive credential prompts. Never echo config/token values.
        print(credentials.redact(str(error)) if isinstance(error,ReleaseError) else 'Release operation failed. Check local configuration and verification results.',file=sys.stderr);sys.exit(1)
