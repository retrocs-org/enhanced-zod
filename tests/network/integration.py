"""Real hub -> agent -> engine authentication and public/private policy checks."""
import contextlib,json,os,pathlib,socket,struct,subprocess,sys,tempfile,time,urllib.request,urllib.error,urllib.parse,re
PHP,AGENT,ENGINE,DRIVER,ROOT=map(str,sys.argv[1:])
_fixture=None;DATA=ROOT
if not (pathlib.Path(ROOT)/'maps/p02_bb_orig01.map').is_file() or os.environ.get('ZOD_TEST_SOURCE_ONLY')=='1':
 sys.path.insert(0,str(pathlib.Path(ROOT)/'tests'));import source_fixtures
 _fixture=tempfile.TemporaryDirectory(prefix='zod-portal-authored-');DATA=_fixture.name;source_fixtures.workspace(ROOT,DATA)
P=json.loads(subprocess.check_output([DRIVER,'protocol'],text=True))
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def api(base,path,body=None,token=None,method=None):
 headers={'Content-Type':'application/json'}
 if token:headers['Authorization']='Bearer '+token
 request=urllib.request.Request(base+path,data=json.dumps(body).encode() if body is not None else None,headers=headers,method=method)
 with urllib.request.urlopen(request,timeout=8) as r:return json.load(r)
def wait(fn,seconds=12):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   value=fn()
   if value:return value
  except (OSError,urllib.error.URLError,ValueError):pass
  time.sleep(.05)
 raise AssertionError('timed out waiting for service')
@contextlib.contextmanager
def process(args,env,log):
 with open(log,'w+') as out:
  p=subprocess.Popen(args,env=env,stdout=out,stderr=subprocess.STDOUT,cwd=DATA)
  try:yield p
  except Exception as e:
   if sys.version_info >= (3,11):e.add_note(pathlib.Path(log).read_text())
   else:print(pathlib.Path(log).read_text(),file=sys.stderr)
   raise
  finally:
   p.terminate()
   try:p.wait(timeout=8)
   except subprocess.TimeoutExpired:p.kill();p.wait()
class Client:
 def __init__(self,p):self.s=socket.create_connection(('127.0.0.1',p),timeout=2);self.s.settimeout(5)
 def send(self,kind,data=b''):self.s.sendall(struct.pack('=ii',len(data),P[kind])+data)
 def exact(self,n):
  b=b''
  while len(b)<n:
   c=self.s.recv(n-len(b));assert c,'connection closed';b+=c
  return b
 def until(self,kind):
  seen=[]
  for _ in range(1000):
   n,k=struct.unpack('=ii',self.exact(8));assert 0<=n<=400000;b=self.exact(n);seen.append((k,b))
   if k==P[kind]:return b,seen
  raise AssertionError('packet not received')
 def close(self):self.s.close()
with tempfile.TemporaryDirectory(prefix='zod-cloud-') as tmp:
 root=pathlib.Path(tmp);env=os.environ.copy();env.update(HOME=tmp,ZOD_PROFILE_DIR=str(root/'profile'),ZOD_CAMPAIGN_DIR=str(root/'campaigns'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',ZOD_LOBBY_COUNTDOWN_SECS='0')
 for k in list(env):
  if k.startswith('ZOD_CLOUD_') or k in ('ZOD_REQUIRE_CLOUD','ZOD_JOIN_TICKET'):env.pop(k)
 key='7'*64;keyfile=root/'key';keyfile.write_text(key)
 hp=port();base=f'http://127.0.0.1:{hp}'
 env.update(ZOD_DEV_HTTP='1',ZOD_PUBLIC_URL=base)
 subprocess.run([PHP,str(pathlib.Path(ROOT)/'public-web/_app/bin/server.php'),'--id','test','--name','Test server','--key-file',str(keyfile)],env=env,cwd=DATA,check=True,capture_output=True)
 with process([PHP,'-S',f'127.0.0.1:{hp}','-t',str(pathlib.Path(ROOT)/'public-web'),str(pathlib.Path(ROOT)/'public-web/router.php')],env,root/'hub.log') as hub:
  wait(lambda:api(base,'/healthz'))
  api(base,'/api/accounts',{'Username':'CloudAlice','Password':'correct horse cloud password'})
  session=api(base,'/api/login',{'Username':'CloudAlice','Password':'correct horse cloud password'})['session']
  api(base,'/api/accounts',{'username':'CloudBob','password':'correct horse cloud password'})
  bob_session=api(base,'/api/login',{'username':'CloudBob','password':'correct horse cloud password'})['session']
  gp=port();state=root/'public'
  args=[AGENT,'--mode','public','--engine',ENGINE,'--data',DATA,'--map','maps/p02_bb_orig01.map','--port',str(gp),'--state',str(state),'--hub',base,'--id','test','--key-file',str(keyfile),'--','-b','blue']
  with process(args,env,root/'public.log'):
   wait(lambda:any(s['id']=='test' for s in api(base,'/api/servers')))
   guest=Client(gp);assert struct.unpack('=i',guest.until('CLOUD_REQUIRED')[0])[0]==1
   guest.send('SET_PLAYER_MODE',bytes([P['PLAYER_MODE']]))
   guest.send('SET_READY',struct.pack('=i',1));guest.send('REQUEST_MAP');guest.send('REQUEST_OBJECTS');guest.send('REQUEST_ZONES');guest.send('REQUEST_VERSION')
   _,seen=guest.until('GIVE_VERSION');assert all(k not in (P['STORE_MAP'],P['ADD_NEW_OBJECT'],P['SET_ZONE_INFO']) for k,_ in seen),'anonymous public world access permitted';guest.close()
   ticket=api(base,'/api/tickets',{'Server':'test'},session)['ticket']
   alice=Client(gp);alice.until('CLOUD_REQUIRED');alice.send('CLOUD_TICKET',ticket.encode()+b'\0')
   assert struct.unpack('=i',alice.until('CLOUD_RESULT')[0])[0]==1
   alice.send('SET_NAME',b'Impostor\0');alice.send('SET_PLAYER_MODE',bytes([P['PLAYER_MODE']]))
   alice.send('SET_TEAM',struct.pack('=i',P['RED_TEAM']));alice.until('SET_TEAM')
   alice.send('REQUEST_MAP');assert alice.until('STORE_MAP')[0]
   alice.send('REQUEST_PLAYER_LIST');alice.send('REQUEST_VERSION')
   _,packets=alice.until('GIVE_VERSION')
   names=[b[4:].split(b'\0')[0].decode() for k,b in packets if k==P['SET_LPLAYER_NAME']]
   assert 'CloudAlice' in names and 'Impostor' not in names,'cloud name can be spoofed'
   alice.send('SET_READY',struct.pack('=i',1));assert alice.until('UPDATE_GAME_PAUSED')[0]==b'\x00'
   # Native menu -> website approval -> agent authentication -> downloaded world.
   native_env=env.copy();native_env.update(ZOD_PORTAL_URL=base,ZOD_PORTAL_DEV_HTTP='1')
   native_env.pop('ZOD_ORCHESTRATOR',None);approval=root/'native-approval-url'
   with process([DRIVER,'portal-join',str(gp),str(approval),'CloudBob'],native_env,root/'native.log') as native:
    url=wait(lambda:approval.read_text() if approval.is_file() and approval.stat().st_size else None)
    code=urllib.parse.parse_qs(urllib.parse.urlparse(url).query)['code'][0]
    request=urllib.request.Request(url,headers={'Authorization':'Bearer '+bob_session})
    with urllib.request.urlopen(request) as response:page=response.read()
    csrf=re.search(rb'name="csrf" value="([a-f0-9]+)"',page).group(1).decode()
    request=urllib.request.Request(base+'/connect/approve',data=urllib.parse.urlencode({'code':code,'csrf':csrf}).encode(),headers={'Authorization':'Bearer '+bob_session,'Content-Type':'application/x-www-form-urlencoded','Cookie':'zod_csrf='+csrf})
    with urllib.request.urlopen(request) as response:assert b'Connection approved' in response.read()
    assert native.wait(timeout=20)==0,(root/'native.log').read_text()


   replay=Client(gp);replay.until('CLOUD_REQUIRED');replay.send('CLOUD_TICKET',ticket.encode()+b'\0');assert struct.unpack('=i',replay.until('CLOUD_RESULT')[0])[0]==0;replay.close()
   # A trusted server grant is required for statistics. Test a separate redeemed
   # ticket and the agent's durable outbox upload/retry boundary.
   claim_ticket=api(base,'/api/tickets',{'Server':'test'},session)['ticket']
   claim=api(base,'/api/tickets/redeem',{'Ticket':claim_ticket},key)
   result={'id':'f'*64,'map':'p02_bb_orig01.map','duration':80,'players':[{'account':claim['account'],'grant':claim['grant'],'won':True,'kills':4,'lost':2,'captures':3}]}
   outbox=state/'outbox'
   report_env=env.copy();report_env['ZOD_CLOUD_OUTBOX']=str(outbox)
   subprocess.run([DRIVER,'cloud-report',claim['account'],claim['grant']],env=report_env,cwd=DATA,check=True,capture_output=True,timeout=20)
   assert list(outbox.glob('*.json')),'engine did not spool result'
   wait(lambda:any(row['username']=='CloudAlice' and row['wins']>=1 for row in api(base,'/api/leaderboard')),30)
   assert not list(outbox.glob('*.json'))
   browser_env=env.copy();browser_env.update(ZOD_PORTAL_URL=base,ZOD_PORTAL_DEV_HTTP='1');browser_env.pop('ZOD_ORCHESTRATOR',None)
   # Heartbeats publish state every twenty seconds; wait for that boundary.
   wait(lambda:any(row['id']=='test' and row['started'] for row in api(base,'/api/servers')),30)
   listing=json.loads(subprocess.check_output([DRIVER,'list-online'],cwd=DATA,env=browser_env,text=True))
   assert any(row['id']=='test' and row['started'] for row in listing),'native browser did not read the public started state'
   alice.close()
   # Fail closed if the account service becomes unavailable.
   offline_ticket=api(base,'/api/tickets',{'Server':'test'},session)['ticket'];hub.terminate();hub.wait(timeout=5)
   offline=Client(gp);offline.until('CLOUD_REQUIRED');offline.send('CLOUD_TICKET',offline_ticket.encode()+b'\0');assert struct.unpack('=i',offline.until('CLOUD_RESULT')[0])[0]==0;offline.close()
  pp=port()
  with process([AGENT,'--mode','private','--engine',ENGINE,'--data',DATA,'--port',str(pp),'--state',str(root/'private')],env,root/'private.log'):
   def connect():
    try:return Client(pp)
    except OSError:return None
   private=wait(connect);private.send('SET_NAME',b'Guest\0');private.send('SET_PLAYER_MODE',bytes([P['PLAYER_MODE']]))
   private.send('SET_TEAM',struct.pack('=i',P['RED_TEAM']));private.until('SET_TEAM');private.send('REQUEST_MAP');assert private.until('STORE_MAP')[0];private.close()
print('Cloud integration passed: public authentication, ticket replay rejection, statistics, outage policy, private guests')
