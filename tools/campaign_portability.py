#!/usr/bin/env python3
"""Check an exported campaign in an isolated process/profile without author files."""
import argparse,json,os,pathlib,subprocess,tempfile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=pathlib.Path,required=True)
    parser.add_argument('--pack',type=pathlib.Path,required=True)
    parser.add_argument('--timeout',type=int,default=120)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='zodee-portability-') as temporary:
        env={key:value for key,value in os.environ.items() if not key.startswith('ZOD_')}
        env.update(HOME=temporary,USERPROFILE=temporary,ZOD_PROFILE_DIR=str(pathlib.Path(temporary)/'profile'),ZOD_CAMPAIGN_DIR=str(pathlib.Path(temporary)/'campaigns'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
        try:
            result=subprocess.run([str(args.game.resolve()),'--verify-campaign-pack',str(args.pack.resolve())],cwd=temporary,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=args.timeout)
            print(json.dumps({'passed':result.returncode==0,'report':result.stdout[-12000:]},indent=2))
            return 0 if result.returncode==0 else 1
        except subprocess.TimeoutExpired:
            print(json.dumps({'passed':False,'report':'Portability test timed out'}));return 1
if __name__=='__main__':raise SystemExit(main())
