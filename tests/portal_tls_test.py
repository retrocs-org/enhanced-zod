"""Real native portal TLS checks: trust, hostname, redirects and response limits."""
import http.server,json,os,pathlib,ssl,subprocess,sys,tempfile,threading,unittest
DRIVER=str(pathlib.Path(sys.argv[1]).resolve());sys.argv=sys.argv[:1]
class PortalTLS(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory(prefix='zod-tls-');root=pathlib.Path(cls.temp.name);cls.ca=root/'ca.crt';key=root/'key.pem'
  config=root/'tls.cnf';config.write_text('[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n[dn]\nCN=localhost\n[ext]\nsubjectAltName=DNS:localhost\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature,keyEncipherment,keyCertSign\n')
  subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(key),'-out',str(cls.ca),'-days','1','-config',str(config)],check=True,capture_output=True,timeout=30)
  cls.calls=[]
  class Handler(http.server.BaseHTTPRequestHandler):
   def do_GET(self):
    cls.calls.append(self.path)
    if self.path.endswith('/redirect'):
     self.send_response(302);self.send_header('Location','https://example.invalid/steal');self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(b'{}');return
    data=b'x'*(1024*1024+1) if self.path.endswith('/large') else json.dumps([{'id':'tls-test','name':'TLS test','host':'127.0.0.1','port':2137,'map':'one.map','players':0,'capacity':2,'started':False,'version':'0.7.0'}]).encode()
    self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers()
    try:self.wfile.write(data)
    except (BrokenPipeError,ConnectionResetError):pass
   def log_message(self,*args):pass
  cls.server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(str(cls.ca),str(key));cls.server.socket=context.wrap_socket(cls.server.socket,server_side=True)
  cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start();cls.base='https://localhost:'+str(cls.server.server_port)+'/index.php'
 @classmethod
 def tearDownClass(cls):cls.server.shutdown();cls.server.server_close();cls.thread.join();cls.temp.cleanup()
 def run_native(self,path='/api/servers',trusted=True,base=None,command='portal-request'):
  env={k:v for k,v in os.environ.items() if not k.startswith('ZOD_')};env['ZOD_PORTAL_URL']=base or self.base
  if trusted:env['ZOD_PORTAL_CA_FILE']=str(self.ca)
  return subprocess.run([DRIVER,command,*([path] if command=='portal-request' else [])],env=env,text=True,capture_output=True,timeout=10)
 def test_trusted_https_listing_is_used_by_native_discovery(self):
  result=self.run_native(command='list-online');self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(json.loads(result.stdout)[0]['id'],'tls-test')
 def test_untrusted_certificate_is_rejected(self):self.assertNotEqual(self.run_native(trusted=False).returncode,0)
 def test_wrong_hostname_is_rejected(self):self.assertNotEqual(self.run_native(base=self.base.replace('localhost','127.0.0.1')).returncode,0)
 def test_redirect_and_oversized_response_are_rejected(self):
  self.assertNotEqual(self.run_native('/redirect').returncode,0);self.assertNotEqual(self.run_native('/large').returncode,0)
 def test_non_https_remote_url_is_rejected_before_network_access(self):
  result=self.run_native(base='http://example.invalid');self.assertNotEqual(result.returncode,0);self.assertIn('HTTPS',result.stderr)
if __name__=='__main__':unittest.main()
