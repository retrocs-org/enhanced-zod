#!/usr/bin/env python3
"""Smoke-test a staged native runtime without developer library-path overrides."""
import argparse,json,os,platform,subprocess,tempfile
from pathlib import Path

def smoke(package,binary):
    package=Path(package).resolve();binary=(package/binary).resolve()
    if not binary.is_file() or package not in binary.parents:raise RuntimeError('Smoke-test binary must be inside its runtime package')
    env={k:v for k,v in os.environ.items() if k not in {'DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','LD_LIBRARY_PATH','PYTHONPATH'} and not k.startswith('ZOD_')}
    if platform.system()=='Windows':env['PATH']=str(package)+os.pathsep+str(Path(env.get('SystemRoot','C:/Windows'))/'System32')
    with tempfile.TemporaryDirectory(prefix='zod-enhanced-smoke-') as directory:
        env['ZOD_PROFILE_DIR']=str(Path(directory)/'profile')
        env['ZOD_CAMPAIGN_DIR']=str(Path(directory)/'campaigns')
        result=subprocess.run([str(binary),'--version'],cwd=directory,env=env,text=True,capture_output=True,timeout=30)
        if result.returncode or 'zod-enhanced' not in result.stdout:raise RuntimeError('Packaged runtime failed its isolated version/loader smoke test: '+result.stderr[-1000:])
        content=subprocess.run([str(binary),'--check-content'],cwd=directory,env=env,text=True,capture_output=True,timeout=30)
        if content.returncode!=2 or 'Original levels: 0/1' not in content.stdout:raise RuntimeError('Fresh package must request imported game content, independently of developer/user files')
        report=subprocess.run([str(binary),'--diagnostics'],cwd=directory,env=env,text=True,capture_output=True,timeout=30)
        report_path=Path(report.stdout.strip())
        if report.returncode or not report_path.is_file() or Path(directory) not in report_path.parents:raise RuntimeError('Fresh packaged diagnostics export failed')
        if directory in report_path.read_text():raise RuntimeError('Diagnostics leaked a personal profile path')
        helpers=[]
        folder=binary.parent;suffix='.exe' if platform.system()=='Windows' else ''
        for name,argument in [('zod-asset-importer','--help'),('ffmpeg','-version'),('ffprobe','-version'),('fluidsynth','--version')]:
            helper=folder/(name+suffix)
            if helper.is_file():
                check=subprocess.run([str(helper),argument],cwd=directory,env=env,text=True,capture_output=True,timeout=45)
                if check.returncode:raise RuntimeError('Packaged helper failed startup: '+name+' '+check.stderr[-1000:])
                helpers.append(name)
    if package.suffix=='.app':subprocess.run(['codesign','--verify','--deep','--strict',str(package)],check=True)
    proof=package.parent/('SMOKE-'+package.name+'.json');proof.write_text(json.dumps({'package':package.name,'binary':str(binary.relative_to(package)),'platform':platform.system(),'passed':True,'helpers':helpers,'checks':['isolated-working-directory','version-command','native-loader','fresh-profile-import-required','diagnostics-export']},indent=2)+'\n')
    print('Packaged runtime smoke test passed:',package.name)
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--package',type=Path,required=True);parser.add_argument('--binary',required=True);args=parser.parse_args();smoke(args.package,args.binary)
