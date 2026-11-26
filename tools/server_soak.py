#!/usr/bin/env python3
"""Repeat real-engine rotations and optional isolated portal cycles; save a reviewable report."""
import argparse,ctypes,datetime,json,os,subprocess,sys,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import source_fixtures

def run(args):
 out=args.output.resolve();out.mkdir(parents=True,exist_ok=False);report={'schema':1,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'passed':False,'rounds':args.rounds,'ticks_per_round':args.ticks,'public_cycles':args.public_cycles,'checks':[],'limitations':['Authored synthetic media and dummy SDL. Accelerated simulation, not a substitute for a wall-clock production endurance test.']};started=time.monotonic()
 try:
  report['build']=json.loads(subprocess.check_output([str(args.engine),'--build-info'],text=True,timeout=10))
  with tempfile.TemporaryDirectory(prefix='zod-soak-') as temp:
   base=Path(temp);data=base/'data';source_fixtures.workspace(str(ROOT),str(data))
   alternate=bytearray((data/'maps/p02_bb_orig01.map').read_bytes());header=source_fixtures.Header.from_buffer(alternate);header.name=b'Alternate authored layout';offset=ctypes.sizeof(source_fixtures.Header)+header.zones*8
   for index in range(header.objects):
    obj=source_fixtures.Object.from_buffer(alternate,offset+index*ctypes.sizeof(source_fixtures.Object));obj.x=header.width-5-obj.x
   (data/'maps/soak-alt.map').write_bytes(alternate)
   env={k:v for k,v in os.environ.items() if not k.startswith('ZOD_')};env.update(HOME=str(base),ZOD_PROFILE_DIR=str(base/'profile'),ZOD_CAMPAIGN_DIR=str(base/'campaigns'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',ZOD_PERSISTENT_SERVER='1')
   with (out/'native.log').open('w') as log:
    subprocess.run([str(args.driver),'soak',str(data/'maps/p02_bb_orig01.map'),str(data/'maps/soak-alt.map'),str(args.rounds),str(args.ticks),str(args.units)],cwd=data,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=args.timeout)
   lines=(out/'native.log').read_text().splitlines();metrics=json.loads(next(line[len('SOAK_REPORT '):] for line in reversed(lines) if line.startswith('SOAK_REPORT ')));report['native']=metrics
   if metrics['warm_rss']==0:report['limitations'].append('Resident-memory telemetry is unavailable on this platform')
   elif metrics['peak_rss_after_warmup']-metrics['warm_rss']>args.max_growth_mib*1024*1024:raise RuntimeError('Resident-memory growth exceeded the configured warm-up allowance')
   report['checks'].append('Repeated same-process map rotations and accelerated simulation')
   for cycle in range(args.public_cycles):
    if not args.agent:raise RuntimeError('--agent is required for public cycles')
    with (out/f'portal-{cycle+1}.log').open('w') as log:
     subprocess.run(['sh',str(ROOT/'tests/network/run-local.sh'),str(args.agent),str(args.engine),str(args.driver)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=args.timeout)
    report['checks'].append(f'Isolated portal cycle {cycle+1}: authentication, reconnects, moderation, outage and maintenance')
  report['passed']=True
 except (OSError,ValueError,RuntimeError,subprocess.SubprocessError,StopIteration) as error:report['failure']=str(error)
 finally:
  report['elapsed_seconds']=round(time.monotonic()-started,3);(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print(('Soak passed: ' if report['passed'] else 'Soak failed: ')+str(out/'report.json'));return 0 if report['passed'] else 1
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--engine',type=Path,required=True);p.add_argument('--driver',type=Path,required=True);p.add_argument('--agent',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--rounds',type=int,default=100);p.add_argument('--ticks',type=int,default=200);p.add_argument('--units',type=int,default=64);p.add_argument('--public-cycles',type=int,default=0);p.add_argument('--max-growth-mib',type=int,default=64);p.add_argument('--timeout',type=int,default=1800);a=p.parse_args()
 for field in ['engine','driver','agent']:
  if getattr(a,field):setattr(a,field,getattr(a,field).resolve())
 if not 4<=a.rounds<=10000 or not 10<=a.ticks<=100000 or not 1<=a.units<=512 or not 0<=a.public_cycles<=100 or a.timeout<30 or a.max_growth_mib<0:p.error('Invalid soak bounds')
 sys.exit(run(a))
