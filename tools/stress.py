#!/usr/bin/env python3
"""Run bounded, isolated local engine/editor stress checks; no hosted runner."""
import argparse,json,os,pathlib,subprocess,sys,tempfile,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import source_fixtures

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver',type=pathlib.Path,default=ROOT/'build/zod_engine_regressions')
    parser.add_argument('--rounds',type=int,default=3)
    parser.add_argument('--ticks',type=int,default=3600,help='0.1 simulated seconds per tick')
    parser.add_argument('--units',type=int,default=256)
    parser.add_argument('--timeout',type=int,default=180,help='wall-clock seconds per subprocess')
    parser.add_argument('--output',type=pathlib.Path,default=pathlib.Path('/tmp/zodee-stress.json'))
    args=parser.parse_args()
    if not (1<=args.rounds<=100 and 1<=args.ticks<=216000 and 1<=args.units<=512 and 1<=args.timeout<=3600):parser.error('Bounds: rounds 1-100, ticks 1-216000, units 1-512, timeout 1-3600')
    driver=str(args.driver.resolve());report={'rounds':[],'passed':True,'ticks':args.ticks,'units':args.units}
    with tempfile.TemporaryDirectory(prefix='zodee-stress-') as temporary:
        folder=pathlib.Path(temporary);workspace=folder/'workspace';source_fixtures.workspace(ROOT,workspace)
        for iteration in range(args.rounds):
            home=folder/str(iteration);home.mkdir()
            env={key:value for key,value in os.environ.items() if not key.startswith('ZOD_')}
            env.update(HOME=str(home),USERPROFILE=str(home),TMPDIR=str(home),ZOD_PROFILE_DIR=str(home/'profile'),ZOD_CAMPAIGN_DIR=str(home/'campaigns'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
            for mode,extra in [('stress',[str(workspace/'maps/p02_bb_orig01.map'),str(args.ticks),str(args.units)]),('enhancements',[str(home)]),('feature-checks',[str(home)]),('replay-feedback',[str(home)]),('authoring-tools',[str(home)]),('script-save',[str(workspace/'maps/p02_bb_orig01.map')])]:
                start=time.monotonic()
                try:
                    result=subprocess.run([driver,mode,*extra],cwd=workspace,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=args.timeout)
                    item={'round':iteration+1,'mode':mode,'seconds':round(time.monotonic()-start,3),'returncode':result.returncode,'log':result.stdout[-24000:]}
                except subprocess.TimeoutExpired as error:
                    item={'round':iteration+1,'mode':mode,'seconds':round(time.monotonic()-start,3),'returncode':-1,'log':'Timed out: '+str(error)}
                report['rounds'].append(item)
                if item['returncode']:report['passed']=False
                print(f"Round {iteration+1}: {mode}: {'PASS' if not item['returncode'] else 'FAIL'} ({item['seconds']}s)",flush=True)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('Report:',args.output)
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
