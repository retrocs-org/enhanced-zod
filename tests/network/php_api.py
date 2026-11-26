"""PHP + real MariaDB contract, migration, concurrency and website tests.
Requires an isolated empty database configured through ZOD_DB_* environment.
"""
import concurrent.futures,shutil,hashlib,http.cookiejar,json,os,pathlib,re,socket,subprocess,sys,tempfile,time,unittest,urllib.request,urllib.error
ROOT=pathlib.Path(__file__).resolve().parents[2];PHP=os.environ.get('PHP_BIN','php');APP=ROOT/'public-web/_app'
def sql(statement):
    code='require '+json.dumps(str(APP/'src/bootstrap.php'))+'; echo json_encode(query('+json.dumps(statement)+')->fetchAll());'
    return json.loads(subprocess.check_output([PHP,'-r',code],text=True))
def request(path,data=None,token='',method=None,headers=None,raw=None,opener=None):
    h={'Content-Type':'application/json',**(headers or {})}
    if token:h['Authorization']='Bearer '+token
    payload=raw if raw is not None else (json.dumps(data).encode() if data is not None else None)
    r=urllib.request.Request(BASE+path,data=payload,headers=h,method=method)
    try:
        with (opener.open(r,timeout=15) if opener else urllib.request.urlopen(r,timeout=15)) as response:return response.status,response.read(),response.headers
    except urllib.error.HTTPError as e:return e.code,e.read(),e.headers

def api(path,data=None,token='',expected=200,method=None):
    status,body,_=request(path,data,token,method)
    assert status==expected,(path,status,body)
    return json.loads(body)

with socket.socket() as s:s.bind(('127.0.0.1',0));PORT=s.getsockname()[1]
BASE=f'http://127.0.0.1:{PORT}'
class PHPBackend(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='zod-php-api-');directory=pathlib.Path(cls.temp.name)
        # Import the old Go file format before using the new service.
        account='b'*64;salt='d'*64;password='legacy test password';grant='e'*64
        legacy={'Accounts':{'legacycommander':{'ID':account,'Username':'LegacyCommander','Salt':salt,'Password':hashlib.pbkdf2_hmac('sha256',password.encode(),bytes.fromhex(salt),600000).hex(),'Created':'2026-01-01T00:00:00Z'}},'Sessions':{},'Tickets':{},'Grants':{},'Results':{'old:1':{'id':'1'*64,'map':'legacy.map','duration':50,'server':'legacy-site','finished':'2026-01-02T00:00:00Z','players':[{'account':account,'grant':grant,'won':True,'kills':2,'lost':1,'captures':1}]}}}
        old=directory/'old.json';old.write_text(json.dumps(legacy))
        dry=subprocess.run([PHP,str(APP/'bin/import-json.php'),'--file',str(old)],check=True,capture_output=True,text=True);assert 'Dry run only' in dry.stdout
        assert not sql('SELECT id FROM accounts')
        subprocess.run([PHP,str(APP/'bin/import-json.php'),'--file',str(old),'--commit'],check=True,capture_output=True)
        assert sql('SELECT id FROM accounts')[0]['id']==account
        cls.key='a'*64;keyfile=directory/'key';keyfile.write_text(cls.key)
        for identifier in ['api-one','api-two']:
            if identifier=='api-two':keyfile.write_text('c'*64)
            subprocess.run([PHP,str(APP/'bin/server.php'),'--id',identifier,'--name',identifier,'--key-file',str(keyfile)],check=True,capture_output=True)
        for identifier in ['api-one','api-two']:sql("INSERT INTO server_owners(server_id,account_id) VALUES('"+identifier+"','"+account+"')")
        env=os.environ.copy();env.update(ZOD_DEV_HTTP='1',ZOD_PUBLIC_URL=BASE,PHP_CLI_SERVER_WORKERS='4',ZOD_ADMIN_USERS='LegacyCommander')
        cls.site=directory/'standalone-site'
        shutil.copytree(ROOT/'public-web',cls.site)
        cls.log=open(directory/'php.log','w+')
        cls.process=subprocess.Popen([PHP,'-S',f'127.0.0.1:{PORT}','-t',str(cls.site),str(cls.site/'router.php')],env=env,stdout=cls.log,stderr=subprocess.STDOUT,start_new_session=True)
        for _ in range(100):
            try:
                if request('/healthz')[0]==200:break
            except OSError:pass
            time.sleep(.03)
        else:
            cls.log.flush(); message=(directory/'php.log').read_text(); cls.tearDownClass(); raise AssertionError('PHP server failed to start: '+message)
    @classmethod
    def tearDownClass(cls):
        import signal
        os.killpg(cls.process.pid,signal.SIGTERM);cls.process.wait(timeout=10);cls.log.close();cls.temp.cleanup()
    def test_01_legacy_account_and_statistics_survive(self):
        me=api('/api/login',{'Username':'LegacyCommander','Password':'legacy test password'})
        self.assertEqual(api('/api/me',token=me['session'])['id'],'b'*64)
        self.assertEqual(sql("SELECT password_scheme FROM accounts WHERE username_key='legacycommander'")[0]['password_scheme'],'argon2id')
        board=api('/api/leaderboard');self.assertEqual(board[0]['username'],'LegacyCommander');self.assertEqual(board[0]['wins'],1)
    def test_02_accounts_tickets_and_idempotent_results(self):
        api('/api/accounts',{'Username':'ApiCommander','Password':'correct test password'},expected=201)
        api('/api/accounts',{'Username':'apicommander','Password':'correct test password'},expected=409)
        api('/api/login',{'Username':'ApiCommander','Password':'incorrect password'},expected=401)
        login=api('/api/login',{'Username':'ApiCommander','Password':'correct test password'});session=login['session']
        me=api('/api/me',token=session);self.assertEqual(me['username'],'ApiCommander')
        encoded=request('/api/login',{'Username':'ApiCommander','Password':'correct test password'})[2].get('Set-Cookie');self.assertIn('HttpOnly',encoded);self.assertIn('SameSite=Strict',encoded)
        heartbeat={'name':'API test server','host':'127.0.0.1','port':2301,'capacity':2,'players':1,'started':False,'map':'arctic.map','version':'test'}
        api('/api/servers/heartbeat',heartbeat,expected=401)
        spoof={**heartbeat,'host':'8.8.8.8'};api('/api/servers/heartbeat',spoof,self.key,expected=400)
        api('/api/servers/heartbeat',heartbeat,self.key)
        self.assertTrue(api('/matches'))
        ticket=api('/api/tickets',{'Server':'api-one'},session)['ticket']
        api('/api/tickets/redeem',{'Ticket':ticket},'c'*64,expected=401)
        def redeem(_):return request('/api/tickets/redeem',{'Ticket':ticket},self.key)
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:responses=list(pool.map(redeem,range(4)))
        self.assertEqual(sorted(r[0] for r in responses),[200,401,401,401])
        claim=json.loads(next(r[1] for r in responses if r[0]==200))
        result={'id':'f'*64,'map':'arctic.map','duration':80,'players':[{'account':claim['account'],'grant':claim['grant'],'won':True,'kills':4,'lost':2,'captures':3}]}
        api('/api/results',result,expected=401)
        invalid={**result,'id':'2'*64,'players':[{**result['players'][0],'account':'0'*64}]};api('/api/results',invalid,self.key,expected=400)
        self.assertFalse(sql("SELECT id FROM matches WHERE match_id='"+'2'*64+"'"))
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:copies=list(pool.map(lambda _:request('/api/results',result,self.key)[0],range(3)))
        self.assertEqual(copies,[200,200,200])
        api('/api/results',{**result,'duration':90},self.key,expected=409)
        row=next(r for r in api('/api/leaderboard') if r['username']=='ApiCommander');self.assertEqual((row['matches'],row['wins'],row['kills']),(1,1,4))
        # Password change rotates the session and revokes old browser sessions.
        changed=api('/api/account/password',{'current_password':'correct test password','password':'a different test password'},session)
        api('/api/me',token=session,expected=401);api('/api/me',token=changed['session'])
        api('/api/logout',{},changed['session']);api('/api/me',token=changed['session'],expected=401)
        # Expired state is enforced by DB time, not by a cleanup job.
        query="UPDATE servers SET last_seen=UTC_TIMESTAMP()-INTERVAL 91 SECOND WHERE id='api-one'";sql(query)
        self.assertFalse(any(r['id']=='api-one' for r in api('/api/servers')))
    def test_03_validation_csrf_and_pages(self):
        for page in ['/','/network','/api','/api-browser','/servers','/leaderboard','/statistics','/downloads','/account','/guide','/players/ApiCommander']:
            status,body,headers=request(page)
            self.assertEqual(status,200,page);self.assertIn(b'<!doctype html>',body.lower());self.assertIn('frame-ancestors',headers['Content-Security-Policy'])
        self.assertEqual(request('/does-not-exist')[0],404)
        self.assertEqual(request('/api/login',raw=b'{not json')[0],400)
        self.assertEqual(request('/api/login',{},headers={'Origin':'https://evil.example'})[0],403)
        self.assertEqual(request('/api/login',raw=b'a'*65537)[0],413)
        # Account forms work without JavaScript and protect their POST actions.
        jar=http.cookiejar.CookieJar();opener=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
        status,body,_=request('/account',opener=opener);csrf=re.search(rb'name="csrf" value="([a-f0-9]+)"',body).group(1).decode()
        data=urllib.parse.urlencode({'csrf':csrf,'username':'ApiCommander','password':'a different test password'}).encode()
        _,body,_=request('/account/login',headers={'Content-Type':'application/x-www-form-urlencoded'},raw=data,opener=opener)
        self.assertIn(b'Welcome back, ApiCommander',body)
        self.assertEqual(request('/account/logout',raw=b'',headers={'Content-Type':'application/x-www-form-urlencoded'})[0],200) # error redirects to visible form
        self.assertEqual(request('/api/statistics')[0],200)
    def test_front_controller_and_directory_routes(self):
        for page in ['features','network','leaderboard','statistics','servers','downloads','account','guide','credits','api-browser']:
            for path in ['/index.php/'+page,'/'+page+'/']:
                status,body,_=request(path)
                self.assertEqual(status,200,path)
                self.assertIn(b'<!doctype html>',body.lower())
                self.assertIn(b'href="/index.php/network"',body)
            self.assertTrue((self.site/page/'index.php').is_file())
        for path in ['/healthz','/api/leaderboard','/api/statistics','/api/servers','/api/catalog']:
            self.assertEqual(api('/index.php'+path),api(path))
        self.assertEqual(api('/index.php/api/login',{'username':'ApiCommander','password':'a different test password'})['username'],'ApiCommander')
        status,body,headers=request('/index.php/account/logout',raw=b'',headers={'Content-Type':'application/x-www-form-urlencoded'})
        self.assertEqual(status,200)
        self.assertIn(b'Your identity on the battlefield',body)
    def test_public_api_browser_catalog(self):
        catalog=api('/api/catalog')
        self.assertTrue(any(row['path']=='/api/servers' and row['browsable'] for row in catalog))
        self.assertTrue(all(row['method']=='GET' and row['access']=='Public' for row in catalog if row['browsable']))
        for row in catalog:
            if row['browsable']: self.assertEqual(request(row['path'])[0],200)
        status,body,_=request('/api-browser')
        self.assertEqual(status,200);self.assertIn(b'id="api-explorer"',body)
        self.assertIn(b'/api/results',body);self.assertIn(b'Server bearer key',body)
        for path in ['/config.inc.php','/src/service.php','/public-web/_app/src/service.php','/sql/001_initial.sql','/bin/migrate.php','/_app/src/service.php','/_app/src/api_catalog.php','/_app/src/views.php','/_app/sql/001_initial.php','/_app/bin/migrate.php']:
            status,body,_=request(path);self.assertEqual(status,404);self.assertNotIn(b'<?php',body)
    def test_04_stored_secrets_are_hashed(self):
        self.assertTrue(all(r['password_hash'].startswith('$argon2id$') for r in sql('SELECT password_hash FROM accounts')))
        self.assertEqual(sql("SELECT key_hash FROM server_keys WHERE id='api-one'")[0]['key_hash'],hashlib.sha256(self.key.encode()).hexdigest())
    def test_05_server_keys_cannot_change_ownership(self):
        keyfile=pathlib.Path(self.temp.name)/'duplicate-key';keyfile.write_text(self.key)
        provision=subprocess.run([PHP,str(APP/'bin/server.php'),'--id','api-two','--key-file',str(keyfile)],capture_output=True,text=True)
        self.assertNotEqual(provision.returncode,0)
        self.assertIn('already assigned',provision.stderr)
        self.assertEqual(sql("SELECT key_hash FROM server_keys WHERE id='api-two'")[0]['key_hash'],hashlib.sha256(('c'*64).encode()).hexdigest())
    def test_06_privacy_friends_blocking_recovery_and_sessions(self):
        sql('DELETE FROM rate_limits')
        users={}
        for name in ['SocialAlpha','SocialBeta','SocialGamma']:
            api('/api/accounts',{'username':name,'password':'a secure test password'},expected=201)
            users[name]=api('/api/login',{'username':name,'password':'a secure test password'})['session']
        a,b,c=[users[name] for name in users]
        prefs={'visibility':'friends','allow_requests':True,'show_online':True}
        api('/api/account/privacy',prefs,b)
        self.assertEqual(request('/players/SocialBeta')[0],404)
        api('/api/community',{'action':'request','username':'SocialBeta'},a)
        self.assertEqual(api('/api/community',token=b)['friends'][0]['status'],'incoming')
        api('/api/community',{'action':'accept','username':'SocialAlpha'},c,expected=404)
        api('/api/community',{'action':'accept','username':'SocialAlpha'},b)
        self.assertEqual(request('/players/SocialBeta',token=a)[0],200)
        self.assertEqual(request('/players/SocialBeta',token=c)[0],404)
        api('/api/servers/heartbeat',{'name':'Social server','host':'127.0.0.1','port':2137,'map':'social.map','players':0,'capacity':2,'started':False,'version':'0.7.0'},self.key)
        api('/api/community',{'action':'invite','username':'SocialBeta','server':'api-one'},a)
        self.assertEqual(api('/api/community',token=b)['invitations'][0]['server_id'],'api-one')
        sql('DELETE FROM rate_limits')
        api('/api/community',{'action':'block','username':'SocialAlpha'},b)
        self.assertEqual(api('/api/community',token=a)['friends'],[])
        self.assertEqual(api('/api/community',token=b)['invitations'],[])
        api('/api/community',{'action':'invite','username':'SocialBeta','server':'api-one'},a,expected=403)
        self.assertEqual(request('/players/SocialBeta',token=a)[0],404)
        api('/api/account/recovery-codes',{'password':'incorrect password'},a,expected=401)
        codes=api('/api/account/recovery-codes',{'password':'a secure test password'},a)['codes']
        self.assertEqual(len(codes),8)
        hashes=[row['code_hash'] for row in sql('SELECT code_hash FROM recovery_codes')]
        self.assertNotIn(codes[0],hashes);self.assertIn(hashlib.sha256(codes[0].encode()).hexdigest(),hashes)
        other=api('/api/login',{'username':'SocialAlpha','password':'a secure test password'})['session']
        sessions=api('/api/account/sessions',token=a);target=next(row['id'] for row in sessions if not row['current'])
        api('/api/account/sessions',{'id':target},b) # Cannot revoke another account's sessions.
        api('/api/me',token=other)
        api('/api/account/sessions',{'id':target},a);api('/api/me',token=other,expected=401)
        api('/api/recover',{'username':'SocialAlpha','code':codes[0],'password':'a recovered test password'})
        api('/api/me',token=a,expected=401)
        api('/api/recover',{'username':'SocialAlpha','code':codes[0],'password':'another test password'},expected=401)
        sql('DELETE FROM rate_limits')
        recovered=api('/api/login',{'username':'SocialAlpha','password':'a recovered test password'})['session']
        for path in ['/account','/community','/creations']:
            self.assertEqual(request(path,token=recovered)[0],200)
        # Private match records are removed from both the board and named recent winners.
        api('/api/account/privacy',{'visibility':'private','allow_requests':False,'show_online':False},recovered)
        self.assertEqual(request('/players/SocialAlpha')[0],404)

    def test_07_creation_upload_preview_moderation_and_versions(self):
        import io,zipfile,base64
        sql('DELETE FROM rate_limits')
        api('/api/accounts',{'username':'MapAuthor','password':'authored test password'},expected=201)
        token=api('/api/login',{'username':'MapAuthor','password':'authored test password'})['session']
        _,page,_=request('/creations',token=token);csrf=re.search(rb'name="csrf" value="([a-f0-9]+)"',page).group(1).decode()
        # Use project-authored map bytes; never upload original game data in tests.
        sys.path.insert(0,str(ROOT/'tests'));import source_fixtures
        map_path=pathlib.Path(self.temp.name)/'community.map';source_fixtures.write_map(map_path)
        image=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aX1cAAAAASUVORK5CYII=')
        def upload(filename,payload,project='',version='1.0.0',auth=None):
            boundary='ZodCommunityBoundary'
            parts=[]
            values={'csrf':csrf,'project':project,'title':'Authored example','description':'Community fixture','version':version,'engine_min':'0.7.0','notes':'Version notes','rights':'yes'}
            for key,value in values.items():parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode())
            for key,name,data in [('creation',filename,payload),('screenshot','preview.png',image)]:parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"; filename="{name}"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()+data+b'\r\n')
            parts.append(f'--{boundary}--\r\n'.encode())
            return request('/creations/submit',token=auth or token,raw=b''.join(parts),headers={'Content-Type':'multipart/form-data; boundary='+boundary,'Cookie':'zod_csrf='+csrf})
        status,body,_=upload('example.map',map_path.read_bytes());self.assertEqual(status,200,body)
        self.assertIn(b'Pending moderator approval',body)
        revision=sql('SELECT id,project_id FROM community_versions ORDER BY created_at DESC LIMIT 1')[0]
        self.assertEqual(api('/api/creations'),[])
        self.assertEqual(request('/creations/download/'+revision['id'])[0],404)
        moderator=APP/'bin/community.php'
        listing=json.loads(subprocess.check_output([PHP,str(moderator),'--list'],text=True));self.assertTrue(any(row['id']==revision['id'] for row in listing))
        review=pathlib.Path(self.temp.name)/'moderation-review'
        subprocess.run([PHP,str(moderator),'--id',revision['id'],'--export',str(review)],check=True,capture_output=True)
        self.assertEqual((review/(revision['id']+'-example.map')).read_bytes(),map_path.read_bytes())
        subprocess.run([PHP,str(moderator),'--id',revision['id'],'--approve'],check=True,capture_output=True)
        subprocess.run([PHP,str(moderator),'--server','api-one','--region','Europe','--mode','Skirmish'],check=True,capture_output=True)
        self.assertEqual(next(row for row in api('/api/servers') if row['id']=='api-one')['region'],'Europe')
        rows=api('/api/creations');self.assertEqual(rows[0]['version'],'1.0.0')
        self.assertEqual(request('/creations/download/'+revision['id'])[1],map_path.read_bytes())
        self.assertEqual(request('/creations/image/'+revision['id'])[2]['Content-Type'],'image/png')
        self.assertEqual(upload('example.map',map_path.read_bytes(),revision['project_id'],'1.1.0')[0],200)
        api('/api/accounts',{'username':'OtherAuthor','password':'authored test password'},expected=201)
        other=api('/api/login',{'username':'OtherAuthor','password':'authored test password'})['session']
        self.assertEqual(upload('example.map',map_path.read_bytes(),revision['project_id'],'2.0.0',other)[0],403)
        self.assertEqual(upload('broken.map',b'truncated')[0],400)
        # Unsafe ZIP entry must fail before storing a new version.
        data=io.BytesIO()
        with zipfile.ZipFile(data,'w') as archive:archive.writestr('../escape.php','<?php');archive.writestr('campaign.ini','format=1\nmap=one.map\n')
        self.assertEqual(upload('bad.zip',data.getvalue())[0],400)
        self.assertEqual(len(sql('SELECT id FROM community_versions')),2)
        valid=io.BytesIO()
        with zipfile.ZipFile(valid,'w') as archive:
            archive.writestr('campaign.ini','format=1\nid=authored-pack\ntitle=Authored pack\nversion=1.0.0\nengine_min=0.7.0\n[mission]\nmap=one.map\n')
            archive.writestr('one.map',map_path.read_bytes())
        self.assertEqual(upload('campaign.zip',valid.getvalue())[0],200)
        self.assertEqual(len(sql('SELECT id FROM community_versions')),3)
        for index in range(8):
            sql("INSERT INTO community_versions(id,project_id,version,engine_min,notes,filename,sha256,manifest,archive,screenshot,screenshot_type) SELECT SHA2(CONCAT(id,'quota"+str(index)+"'),256),project_id,'9.0."+str(index)+"',engine_min,notes,filename,sha256,manifest,archive,screenshot,screenshot_type FROM community_versions WHERE id='"+revision['id']+"'")
        status,body,_=upload('example.map',map_path.read_bytes())
        self.assertEqual(status,429);self.assertIn(b'reviewed',body)


    def test_11_server_operations_roles_bans_rotation_presets_health_and_owner_security(self):
        sql('DELETE FROM rate_limits')
        owner=api('/api/login',{'username':'ApiCommander','password':'a different test password'})['session']
        admin=api('/api/login',{'username':'LegacyCommander','password':'legacy test password'})['session']
        people={}
        for name in ['OpsModerator','OpsPlayer']:
            api('/api/accounts',{'username':name,'password':'server operations test password'},expected=201)
            people[name]=api('/api/login',{'username':name,'password':'server operations test password'})['session']
        moderator=people['OpsModerator'];player=people['OpsPlayer'];key='3'*64
        request_id=api('/api/server-auth/start',{'server':'ops-server','name':'Operations','key':key})
        api('/api/server-auth/approve',{'code':request_id['user_code']},owner)
        api('/api/servers/heartbeat',{'name':'Operations','host':'127.0.0.1','port':2301,'map':'maps/a.map','capacity':2,'players':1,'started':True,'version':'0.7.0'},key)
        api('/api/server-console?server=ops-server',token=admin,expected=403) # site operator is not a server owner
        sql('DELETE FROM rate_limits')
        api('/api/my-servers/admin',{'server':'ops-server','username':'LegacyCommander','action':'add','role':'admin'},owner)
        api('/api/my-servers/admin',{'server':'ops-server','username':'OpsModerator','action':'add','role':'moderator'},owner)
        ids={name:api('/api/me',token=token)['id'] for name,token in [('owner',owner),('mod',moderator),('player',player)]}
        maps=[{'map':'maps/'+name+'.map','title':name,'width':64,'height':80,'forts':[[4,4,1],[50,60,2]],'zones':[[0,0,32,80]]} for name in ['a','b']]
        snapshot={'maps':maps,'roster':[{'account':ids['owner'],'name':'ApiCommander'},{'account':ids['player'],'name':'OpsPlayer'}],'round':'ops-round','intermission':True,'vote_remaining':25,'tick_ms':2.5,'uptime':40,'role_age':1,'outbox':2,'cloud_ok':True,'next_map':'maps/b.map','applied_revision':0}
        api('/api/server/health',{'snapshot':snapshot},key)
        api('/api/server-console?server=ops-server',token=player,expected=403)
        self.assertEqual(api('/api/server-console?server=ops-server',token=moderator)['role'],1)
        settings={'rotation':['maps/a.map','maps/b.map'],'bots':[2],'difficulty':2,'speed':150,'voting':True}
        api('/api/server-console',{'server':'ops-server','action':'configure','settings':settings},moderator,expected=403)
        api('/api/server-console',{'server':'ops-server','action':'configure','settings':settings},admin)
        api('/api/server-console',{'server':'ops-server','action':'configure','settings':{**settings,'rotation':['/private/secret.map']}},admin,expected=400)
        api('/api/server-console',{'server':'ops-server','action':'save-preset','name':'Practice','settings':settings},admin)
        api('/api/server-console',{'server':'ops-server','action':'load-preset','name':'Practice'},admin)
        console=api('/api/server-console?server=ops-server',token=owner);self.assertEqual(console['config']['settings'],settings);self.assertTrue(console['health']['fresh']);self.assertTrue(console['audit'])
        self.assertEqual(request('/server-console?server=ops-server',token=moderator)[0],200)
        view=api('/api/server-rotation?server=ops-server');self.assertEqual(len(view['maps']),2);self.assertNotIn('map',view['maps'][0]);self.assertIn(b'<svg',request('/server-rotation?server=ops-server')[1])
        api('/api/server-rotation/vote',{'server':'ops-server','round':'ops-round','map':view['maps'][1]['id']},player)
        api('/api/server-rotation/vote',{'server':'ops-server','round':'ops-round','map':view['maps'][0]['id']},player)
        self.assertEqual(len(sql("SELECT * FROM server_map_votes WHERE server_id='ops-server'")),1)
        api('/api/server-rotation/vote',{'server':'ops-server','round':'old-round','map':view['maps'][0]['id']},player,expected=409)
        api('/api/server-rotation/vote',{'server':'ops-server','round':'ops-round','map':view['maps'][0]['id']},admin,expected=403)
        sql('DELETE FROM rate_limits')
        ticket=api('/api/tickets',{'server':'ops-server'},player)['ticket']
        api('/api/server-console',{'server':'ops-server','action':'ban','username':'ApiCommander','reason':'no','minutes':15},moderator,expected=403)
        api('/api/server-console',{'server':'ops-server','action':'kick','username':'LegacyCommander','reason':'no'},moderator,expected=403)
        api('/api/server-console',{'server':'ops-server','action':'ban','username':'OpsPlayer','reason':'Temporary test ban','minutes':15},moderator)
        api('/api/tickets',{'server':'ops-server'},player,expected=403)
        api('/api/tickets/redeem',{'ticket':ticket},key,expected=403)
        control=api('/api/server/control',token=key);self.assertIn(ids['mod'],control['moderators']);self.assertTrue(control['bans']);self.assertTrue(control['commands'])
        command=str(control['commands'][0]['id']);api('/api/server/control/ack',{'results':[{'id':command,'applied':True}]},'a'*64)
        self.assertTrue(api('/api/server/control',token=key)['commands'])
        api('/api/server/control/ack',{'results':[{'id':command,'applied':True}]},key)
        self.assertFalse(api('/api/server/control',token=key)['commands'])
        api('/api/server-console',{'server':'ops-server','action':'unban','username':'OpsPlayer'},moderator)
        self.assertTrue(api('/api/tickets',{'server':'ops-server'},player)['ticket'])
        for action in ['disable','enable','revoke-host']:api('/api/server-console',{'server':'ops-server','action':action},admin,expected=403)
        sql('DELETE FROM rate_limits')
        api('/api/server-console',{'server':'ops-server','action':'disable'},owner);api('/api/server/identity',token=key,expected=401)
        api('/api/server-console',{'server':'ops-server','action':'enable'},owner);self.assertEqual(api('/api/server/identity',token=key)['id'],'ops-server')
        api('/api/server-console',{'server':'ops-server','action':'revoke-host'},owner);api('/api/server/identity',token=key,expected=401)
        replacement=api('/api/server-auth/start',{'server':'ops-server','name':'Renewed','key':'0'*64});api('/api/server-auth/approve',{'code':replacement['user_code']},owner)
        self.assertEqual(api('/api/server/identity',token='0'*64)['id'],'ops-server')
        self.assertEqual(request('/server-console',token=owner,raw=b'server=ops-server&action=disable',headers={'Content-Type':'application/x-www-form-urlencoded'})[0],403)

    def test_10_server_ownership_and_owner_only_admin_delegation(self):
        sql('DELETE FROM rate_limits')
        owner=api('/api/login',{'username':'ApiCommander','password':'a different test password'})['session']
        other=api('/api/login',{'username':'LegacyCommander','password':'legacy test password'})['session']
        key='6'*64;started=api('/api/server-auth/start',{'server':'owner-new','name':'Owned server','key':key})
        self.assertEqual(api('/api/server-auth/poll',{'device':started['device']})['state'],'pending')
        self.assertEqual(request('/server-auth?code='+started['user_code'])[0],200)
        self.assertEqual(request('/server-auth/approve',raw=('code='+started['user_code']).encode(),headers={'Content-Type':'application/x-www-form-urlencoded'},token=owner)[0],403)
        api('/api/server-auth/approve',{'code':started['user_code']},expected=401)
        api('/api/server-auth/approve',{'code':started['user_code']},owner)
        api('/api/server-auth/approve',{'code':started['user_code']},other,expected=409)
        sql("UPDATE server_auth_requests SET last_poll=NULL WHERE user_code='"+started['user_code']+"'")
        claimed=api('/api/server-auth/poll',{'device':started['device']});self.assertEqual(claimed['state'],'approved')
        api('/api/server-auth/poll',{'device':started['device']},expected=401)
        identity=api('/api/server/identity',token=key);self.assertEqual(identity['username'],'ApiCommander');self.assertEqual(identity['owner'],claimed['owner'])
        api('/api/my-servers',expected=401)
        self.assertEqual(next(row for row in api('/api/my-servers',token=owner) if row['id']=='owner-new')['role'],'owner')
        api('/api/my-servers/admin',{'server':'owner-new','username':'LegacyCommander','action':'add'},other,expected=403)
        api('/api/my-servers/admin',{'server':'owner-new','username':'LegacyCommander','action':'add'},owner)
        self.assertIn('b'*64,api('/api/server/admins',token=key)['admins'])
        self.assertEqual(next(row for row in api('/api/my-servers',token=other) if row['id']=='owner-new')['role'],'administrator')
        api('/api/my-servers/admin',{'server':'owner-new','username':'ApiCommander','action':'remove'},owner,expected=400)
        api('/api/my-servers/admin',{'server':'owner-new','username':'ApiCommander','action':'add'},other,expected=403)
        api('/api/my-servers/admin',{'server':'api-one','username':'ApiCommander','action':'add'},owner,expected=403)
        self.assertEqual(request('/my-servers',token=owner)[0],200)
        self.assertEqual(request('/my-servers/admin',token=owner,raw=b'server=owner-new',headers={'Content-Type':'application/x-www-form-urlencoded'})[0],403)
        api('/api/my-servers/admin',{'server':'owner-new','username':'LegacyCommander','action':'remove'},owner)
        self.assertEqual(api('/api/server/admins',token=key)['admins'],[])
        sql('DELETE FROM rate_limits')
        bad=api('/api/server-auth/start',{'server':'owner-new','name':'Takeover','key':'9'*64})
        api('/api/server-auth/approve',{'code':bad['user_code']},other,expected=403)
        takeover=api('/api/server-auth/start',{'server':'owner-new','name':'Existing owner','key':key})
        api('/api/server-auth/approve',{'code':takeover['user_code']},other,expected=403)
        api('/api/server-auth/approve',{'code':takeover['user_code']},owner)
        api('/api/admin/actions',{'action':'disable-server','target':'owner-new'},other)
        api('/api/server/identity',token=key,expected=401)
        api('/api/server-auth/approve',{'code':takeover['user_code']},owner)
        api('/api/server/identity',token=key,expected=401) # account ownership never bypasses a site disable
        api('/api/admin/actions',{'action':'enable-server','target':'owner-new'},other)
        cancelled=api('/api/server-auth/start',{'server':'cancel-host','name':'Cancelled','key':'8'*64})
        api('/api/server-auth/cancel',{'device':cancelled['device']});api('/api/server-auth/poll',{'device':cancelled['device']},expected=401)
        expired=api('/api/server-auth/start',{'server':'expire-host','name':'Expired','key':'8'*64})
        sql("UPDATE server_auth_requests SET expires_at=UTC_TIMESTAMP()-INTERVAL 1 SECOND WHERE user_code='"+expired['user_code']+"'")
        api('/api/server-auth/approve',{'code':expired['user_code']},owner,expected=404)
        self.assertFalse(sql("SELECT id FROM server_keys WHERE id IN ('cancel-host','expire-host')"))
        self.assertNotIn(key,json.dumps(sql('SELECT * FROM server_keys')))
        sql('DELETE FROM rate_limits')
        sql("INSERT INTO server_keys(id,name,key_hash) VALUES('legacy-unowned','Legacy',SHA2('"+'5'*64+"',256))")
        sql("INSERT INTO servers(id,name,host,port,players,capacity,started,last_seen) VALUES('legacy-unowned','Legacy','127.0.0.1',2137,0,2,0,UTC_TIMESTAMP(6))")
        self.assertFalse(any(row['id']=='legacy-unowned' for row in api('/api/servers')))
        api('/api/tickets',{'server':'legacy-unowned'},owner,expected=404)
        api('/api/server/identity',token='5'*64,expected=401)
        old=api('/api/server-auth/start',{'server':'legacy-unowned','name':'Legacy','key':'5'*64})
        api('/api/server-auth/approve',{'code':old['user_code']},owner)
        self.assertEqual(api('/api/server/identity',token='5'*64)['owner'],identity['owner'])
        # An established owner can authorize a replacement host without the old credential.
        sql('DELETE FROM rate_limits')
        stale=api('/api/server-auth/start',{'server':'owner-new','name':'Stale host','key':key})
        replacement=api('/api/server-auth/start',{'server':'owner-new','name':'Replacement host','key':'4'*64})
        api('/api/server-auth/approve',{'code':replacement['user_code']},owner)
        api('/api/server/identity',token=key,expected=401)
        self.assertEqual(api('/api/server/identity',token='4'*64)['owner'],identity['owner'])
        api('/api/server-auth/approve',{'code':stale['user_code']},owner,expected=409)



    def test_09_operator_access_reports_visibility_and_audit(self):
        sql('DELETE FROM rate_limits')
        admin=api('/api/login',{'username':'LegacyCommander','password':'legacy test password'})['session']
        user=api('/api/login',{'username':'ApiCommander','password':'a different test password'})['session']
        api('/api/admin',expected=401);api('/api/admin',token=user,expected=403)
        queue=api('/api/admin',token=admin);self.assertTrue(queue['servers']);self.assertTrue(queue['creations'])
        revision=queue['creations'][0]['id']
        api('/api/admin/actions',{'action':'approve','target':revision},user,expected=403)
        api('/api/admin/actions',{'action':'approve','target':revision},admin)
        self.assertEqual(request('/creations/download/'+revision)[0],200)
        api('/api/reports',{'kind':'creation','target':revision,'reason':'Please review this example.'},user)
        reports=api('/api/admin',token=admin)['reports'];self.assertTrue(reports)
        api('/api/admin/actions',{'action':'resolve','target':reports[0]['id']},admin)
        self.assertFalse(api('/api/admin',token=admin)['reports'])
        api('/api/admin/actions',{'action':'hide','target':revision},admin)
        self.assertEqual(request('/creations/download/'+revision)[0],404)
        self.assertEqual(request('/creations/download/'+revision,token=admin)[0],200)
        self.assertEqual(request('/admin',token=admin)[0],200)
        self.assertEqual(request('/admin',token=user)[0],403)
        self.assertEqual(request('/admin/action',token=admin,raw=b'action=approve&target='+revision.encode(),headers={'Content-Type':'application/x-www-form-urlencoded'})[0],403)
        api('/api/admin/actions',{'action':'disable-server','target':'api-one'},admin)
        self.assertFalse(any(row['id']=='api-one' for row in api('/api/servers')))
        api('/api/admin/actions',{'action':'enable-server','target':'api-one'},admin)
        self.assertTrue(api('/api/admin',token=admin)['audit'])

    def test_08_native_browser_approval_scope_cancellation_and_single_use(self):
        sql('DELETE FROM rate_limits')
        api('/api/servers/heartbeat',{'name':'Approval server','host':'127.0.0.1','port':2137,'map':'approval.map','players':0,'capacity':2,'started':False,'version':'0.7.0'},self.key)
        login=api('/api/login',{'username':'ApiCommander','password':'a different test password'})['session']
        started=api('/api/connect/start',{'server':'api-one'})
        self.assertEqual(len(started['device']),64);self.assertEqual(len(started['user_code']),12)
        self.assertEqual(api('/api/connect/poll',{'device':started['device']})['state'],'pending')
        self.assertEqual(request('/connect?code='+started['user_code'])[0],200)
        # Only a signed-in browser with CSRF can approve; tokens never enter URLs.
        denied=request('/connect/approve',raw=urllib.parse.urlencode({'code':started['user_code']}).encode(),headers={'Content-Type':'application/x-www-form-urlencoded'})
        self.assertEqual(denied[0],403)
        _,page,_=request('/connect?code='+started['user_code'],token=login)
        csrf=re.search(rb'name="csrf" value="([a-f0-9]+)"',page).group(1).decode()
        status,body,_=request('/connect/approve',token=login,raw=urllib.parse.urlencode({'code':started['user_code'],'csrf':csrf}).encode(),headers={'Content-Type':'application/x-www-form-urlencoded','Cookie':'zod_csrf='+csrf})
        self.assertEqual(status,200,body);self.assertIn(b'Connection approved',body)
        sql('UPDATE native_connections SET last_poll=NULL')
        approved=api('/api/connect/poll',{'device':started['device']});self.assertEqual(approved['state'],'approved');self.assertEqual(approved['server'],'api-one')
        api('/api/tickets/redeem',{'ticket':approved['ticket']},'c'*64,expected=401)
        claim=api('/api/tickets/redeem',{'ticket':approved['ticket']},self.key);self.assertEqual(claim['username'],'ApiCommander')
        api('/api/connect/poll',{'device':started['device']},expected=401)
        api('/api/tickets/redeem',{'ticket':approved['ticket']},self.key,expected=401)
        pending=api('/api/connect/start',{'server':'api-one'})
        api('/api/connect/cancel',{'device':pending['device']});api('/api/connect/poll',{'device':pending['device']},expected=401)
        expired=api('/api/connect/start',{'server':'api-one'});sql('UPDATE native_connections SET expires_at=UTC_TIMESTAMP()-INTERVAL 1 SECOND')
        api('/api/connect/poll',{'device':expired['device']},expected=401)
        self.assertNotIn(started['device'],str(sql('SELECT * FROM native_connections')))

    def test_12_history_native_moderation_and_maintenance(self):
        sql('DELETE FROM rate_limits')
        # Existing test account session is refreshed without depending on password changes.
        owner=api('/api/login',{'username':'OpsModerator','password':'server operations test password'})['session']
        actor=api('/api/me',token=owner)['id'];target=sql("SELECT id FROM accounts WHERE username='OpsPlayer'")[0]['id']
        # ops-server was reauthorized with a new scoped credential by test_11.
        key='0'*64
        api('/api/servers/heartbeat',{'name':'Operations','host':'127.0.0.1','port':2301,'map':'maps/a.map','capacity':2,'players':1,'started':True,'version':'test'},key)
        api('/api/server/moderate',{'actor':actor,'target':target,'action':'kick','reason':'Native request','minutes':60},key)
        api('/api/server/moderate',{'actor':target,'target':actor,'action':'ban','reason':'Unauthorized','minutes':60},key,expected=403)
        api('/api/server-console',{'server':'ops-server','action':'schedule-maintenance','delay':30,'reason':'Update','maintenance_action':'restart'},owner,expected=403)
        # Use the immutable owner's session from the existing ownership fixture.
        owner_id=sql("SELECT account_id FROM server_owners WHERE server_id='ops-server'")[0]['account_id']
        session='9'*64;sql("INSERT INTO sessions(token_hash,account_id,expires_at) VALUES('"+hashlib.sha256(session.encode()).hexdigest()+"','"+owner_id+"',UTC_TIMESTAMP()+INTERVAL 1 HOUR)")
        api('/api/server-console',{'server':'ops-server','action':'schedule-maintenance','delay':30,'reason':'Update','maintenance_action':'restart'},session)
        control=api('/api/server/control',token=key);at=int(control['maintenance']['at']);self.assertGreater(at,time.time());self.assertEqual(control['maintenance']['action'],'restart')
        sql("UPDATE server_owners SET maintenance_at=UNIX_TIMESTAMP()-1 WHERE server_id='ops-server'")
        api('/api/tickets',{'server':'ops-server'},owner,expected=503)
        api('/api/server/maintenance-complete',{'at':at},key) # stale completion cannot clear a newer schedule
        self.assertNotEqual(int(api('/api/server/control',token=key)['maintenance']['at']),0)
        api('/api/server-console',{'server':'ops-server','action':'cancel-maintenance'},session)
        self.assertEqual(int(api('/api/server/control',token=key)['maintenance']['at']),0)
        history=api('/api/matches');self.assertTrue(history)
        match=api('/api/match?id='+str(history[0]['id']));self.assertTrue(match['players']);self.assertIn('team',match['players'][0]);self.assertNotIn('grant',str(match));self.assertNotIn('payload_hash',match)
        self.assertEqual(request('/match-history?id='+str(history[0]['id']))[0],200)
        api('/api/matches?before=oops',expected=400)

if __name__=='__main__':unittest.main()
