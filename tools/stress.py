#!/usr/bin/env python3
"""Run bounded, isolated local engine/editor stress checks; no hosted runner."""
import argparse,json,re,os,pathlib,subprocess,sys,tempfile,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import source_fixtures

def assess_metrics(metrics,ticks,memory_growth_mib):
    problems=[]
    required=['ticks','reloads','moved','damaged','dead','live_units','survivors','warm_rss','final_rss','peak_rss']
    if any(type(metrics.get(key)) is not int or metrics[key]<0 for key in required):return ['Invalid or missing stress measurements']
    if metrics.get('ticks')!=ticks or metrics.get('reloads',0)<(ticks+99)//100:problems.append('Incomplete simulated run/save cycles')
    if ticks>=120 and metrics.get('moved',0)<=0:problems.append('No movement observed')
    if ticks>=600 and metrics.get('damaged',0)<=0:problems.append('No combat damage observed')
    if metrics.get('live_units',0)<=0:problems.append('No surviving units')
    if metrics.get('survivors',0)<=0:problems.append('World lost all objects')
    warm,final=metrics.get('warm_rss',0),metrics.get('final_rss',0)
    if warm and final and max(final,metrics.get('peak_rss',final))-warm>memory_growth_mib*1024*1024:problems.append('Resident memory growth exceeded threshold')
    return problems

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver',type=pathlib.Path,default=ROOT/'build/zod_engine_regressions')
    parser.add_argument('--rounds',type=int,default=3)
    parser.add_argument('--ticks',type=int,default=3600,help='0.1 simulated seconds per tick')
    parser.add_argument('--units',type=int,default=256)
    parser.add_argument('--timeout',type=int,default=180,help='wall-clock seconds per subprocess')
    parser.add_argument('--memory-growth-mib',type=int,default=32,help='maximum resident growth after warm-up')
    parser.add_argument('--output',type=pathlib.Path,default=pathlib.Path('/tmp/zodee-stress.json'))
    args=parser.parse_args()
    if not (1<=args.rounds<=100 and 1<=args.ticks<=216000 and 1<=args.units<=512 and 1<=args.timeout<=3600 and 0<=args.memory_growth_mib<=4096):parser.error('Bounds: rounds 1-100, ticks 1-216000, units 1-512, timeout 1-3600')
    driver=str(args.driver.resolve());report={'rounds':[],'passed':True,'ticks':args.ticks,'units':args.units,'memory_growth_limit_mib':args.memory_growth_mib}
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
                    if mode=='stress':
                        matches=re.findall(r'^STRESS_METRICS (.+)$',result.stdout,re.M)
                        if not matches:item.update(returncode=1,assessment='Missing stress metrics')
                        else:
                            try:
                                metrics=json.loads(matches[-1]);item['metrics']=metrics
                                problems=assess_metrics(metrics,args.ticks,args.memory_growth_mib)
                            except (ValueError,TypeError):problems=['Invalid stress metrics']
                            if problems:item.update(returncode=1,assessment=problems)
                except subprocess.TimeoutExpired as error:
                    item={'round':iteration+1,'mode':mode,'seconds':round(time.monotonic()-start,3),'returncode':-1,'log':'Timed out: '+str(error)}
                report['rounds'].append(item)
                if item['returncode']:report['passed']=False
                print(f"Round {iteration+1}: {mode}: {'PASS' if not item['returncode'] else 'FAIL'} ({item['seconds']}s)",flush=True)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('Report:',args.output)
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
