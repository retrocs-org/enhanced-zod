#!/usr/bin/env python3
"""Check a staged native package's first-run, import and multiplayer lifecycle.

Run on each target OS with its matching engine regression driver. Uses only
project-authored test levels, never a user's game media, account or save files.
"""
import argparse,hashlib,json,os,platform,subprocess,sys,tempfile
from pathlib import Path
from package_smoke import smoke
ROOT=Path(__file__).resolve().parents[1]

def inventory(package):
    result={}
    for path in sorted(package.rglob('*')):
        if path.is_file():
            digest=hashlib.sha256()
            with path.open('rb') as stream:
                for chunk in iter(lambda:stream.read(1024*1024),b''):digest.update(chunk)
            result[path.relative_to(package).as_posix()]=digest.hexdigest()
    return result

def check(package,binary,driver):
    package=Path(package).resolve();driver=Path(driver).resolve();executable=(package/binary).resolve()
    receipt=package.parent/('INSTALL-CHECK-'+package.name+'.json')
    receipt.unlink(missing_ok=True) # A failed rerun must not retain an old pass.
    if not executable.is_file() or package not in executable.parents:raise RuntimeError('Select a binary inside the staged package')
    if not driver.is_file():raise RuntimeError('Build the native zod_engine_regressions driver first')
    smoke(package,binary)
    before=inventory(package)
    env={k:v for k,v in os.environ.items() if not k.startswith('ZOD_') and k not in {'DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','LD_LIBRARY_PATH','PYTHONPATH'}}
    env['ZOD_TEST_SOURCE_ONLY']='1'
    if platform.system()=='Windows':env['PATH']=str(executable.parent)+os.pathsep+str(Path(env.get('SystemRoot','C:/Windows'))/'System32')
    with tempfile.TemporaryDirectory(prefix='zod-install-check-') as temporary:
        subprocess.run([sys.executable,str(ROOT/'tests/engine_regressions.py'),str(executable),str(driver),str(ROOT),'--installation-check'],cwd=temporary,env=env,check=True,timeout=240)
    if before!=inventory(package):raise RuntimeError('Installation checks modified staged package files')
    proof={'platform':platform.system(),'architecture':platform.machine(),'package':package.name,'binary':binary,'passed':True,'files':before,
           'checks':['two-actual-clients-ready-reconnect-combat-summary','verified-map-cache-reuse','actual-app-campaign-level-render','actual-app-offline-ai-render','actual-app-network-client-render','fresh-profile-import-screen','authored-level-import','campaign-start-save-reload','two-player-match-startup','direct-join-version-check','connection-loss-recovery','small-stack-socket-replies','embedded-worker-map-transfer'],
           'limitations':['Authored placeholder media and dummy SDL renderer exercise the real app; real display, audible output, original-disc import and manual campaign-menu navigation require manual QA.']}
    temporary=receipt.with_suffix('.tmp');temporary.write_text(json.dumps(proof,indent=2)+'\n');temporary.replace(receipt)
    print('Native installation check passed:',receipt)
    return receipt

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--package',type=Path,required=True);parser.add_argument('--binary',required=True);parser.add_argument('--driver',type=Path,required=True);args=parser.parse_args()
    try:check(args.package,args.binary,args.driver)
    except (RuntimeError,subprocess.SubprocessError) as error:parser.exit(1,str(error)+'\n')
