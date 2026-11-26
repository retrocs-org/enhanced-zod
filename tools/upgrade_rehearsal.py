#!/usr/bin/env python3
"""Rehearse package replacement and rollback with an isolated persistent profile.

Copies packages and an optional profile template; never edits the supplied trees.
A report proves loader startup and file preservation, not every old save format.
"""
import argparse,hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path
from installation_check import inventory

def copy_tree(source,target):
    # Package library symlinks are part of a native bundle. Profile links are not accepted.
    shutil.copytree(source,target,symlinks=True)

def rehearse(previous,candidate,binary,report,profile_template=None):
    previous=Path(previous).resolve();candidate=Path(candidate).resolve();report=Path(report).resolve()
    relative=Path(binary)
    if relative.is_absolute() or '..' in relative.parts:raise RuntimeError('Binary must be relative to each package')
    for package in (previous,candidate):
        executable=(package/relative).resolve()
        if not executable.is_file() or package not in executable.parents:raise RuntimeError('Binary must exist inside both packages')
    if report==previous or previous in report.parents or report==candidate or candidate in report.parents:raise RuntimeError('Report must be outside supplied packages')
    if profile_template:
        source=Path(profile_template).resolve()
        if report==source or source in report.parents:raise RuntimeError("Report must be outside the profile template")
    report.unlink(missing_ok=True)
    before={str(p):inventory(p) for p in {previous,candidate}}
    with tempfile.TemporaryDirectory(prefix='zod-upgrade-rehearsal-') as tmp:
        tmp=Path(tmp).resolve();profile=tmp/'profile';home=tmp/'home';home.mkdir()
        if profile_template:
            source=Path(profile_template).resolve()
            if not source.is_dir() or any(p.is_symlink() for p in source.rglob('*')):raise RuntimeError('Profile template must be a directory without symlinks')
            shutil.copytree(source,profile)
        else:
            profile.mkdir()
            for name,data in {'preferences.ini':b'music_volume=37\neffects_volume=63\n','saves/preservation.fixture':b'authored-save-preservation-fixture','content/maps/preservation.fixture':b'authored-import-preservation-fixture','campaign-progress/preservation.fixture':b'authored-progress-preservation-fixture'}.items():
                path=profile/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        preserved=inventory(profile)
        env={k:v for k,v in os.environ.items() if not k.startswith('ZOD_') and k not in {'DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','LD_LIBRARY_PATH','PYTHONPATH'}}
        env.update(ZOD_PROFILE_DIR=str(profile),ZOD_CAMPAIGN_DIR=str(profile/'campaigns'),HOME=str(home),USERPROFILE=str(home))
        phases=[];active=tmp/'active';rollback=tmp/'rollback';staged=tmp/'staged'
        def probe(phase):
            executable=str(active/relative)
            identity=subprocess.run([executable,'--build-info'],cwd=tmp,env=env,capture_output=True,text=True,check=True,timeout=30)
            build=json.loads(identity.stdout)
            result=subprocess.run([executable,'--diagnostics'],cwd=tmp,env=env,capture_output=True,text=True,check=True,timeout=30)
            output=Path(result.stdout.strip()).resolve()
            if profile not in output.parents or not output.is_file():raise RuntimeError('Diagnostics escaped isolated profile')
            actual=inventory(profile)
            if any(actual.get(name)!=digest for name,digest in preserved.items()):raise RuntimeError('Upgrade or rollback changed pre-existing profile files')
            phases.append({'phase':phase,'build_id':build['build_id'],'preserved_files':len(preserved)})
        copy_tree(previous,active);probe('previous-start')
        copy_tree(candidate,staged);active.rename(rollback);staged.rename(active);probe('updated-start')
        shutil.rmtree(active);rollback.rename(active);probe('rollback-start')
    if any(inventory(Path(p))!=files for p,files in before.items()):raise RuntimeError('Rehearsal modified a supplied package')
    proof={'passed':True,'phases':phases,'checks':['isolated-profile','package-replacement','loader-startup','diagnostics-startup','settings-saves-imports-preserved','rollback','input-packages-unchanged'],'limitations':['Startup and byte preservation only; run installation-check and load representative real saves for cross-version gameplay compatibility.'],'profile_template':bool(profile_template)}
    report.parent.mkdir(parents=True,exist_ok=True);temporary=report.with_suffix('.tmp');temporary.write_text(json.dumps(proof,indent=2)+'\n');temporary.replace(report);return proof

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--previous',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--binary',required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--profile-template',type=Path);a=p.parse_args()
    try:rehearse(a.previous,a.candidate,a.binary,a.report,a.profile_template);print('Upgrade and rollback rehearsal passed:',a.report)
    except (OSError,RuntimeError,ValueError,subprocess.SubprocessError) as error:p.exit(1,str(error)+'\n')
