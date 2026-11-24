"""Check local PHP settings and environment/secret-file precedence."""
import json,os,pathlib,shutil,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
@unittest.skipUnless(shutil.which('php'),'PHP CLI required')
class Config(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.site=pathlib.Path(self.temp.name);(self.site/'_app/src').mkdir(parents=True)
        shutil.copyfile(ROOT/'public-web/_app/src/bootstrap.php',self.site/'_app/src/bootstrap.php')
        self.env={k:v for k,v in os.environ.items() if not k.startswith('ZOD_')}
    def config(self,values):
        # json_decode avoids interpreting test strings as PHP code.
        payload=json.dumps(json.dumps(values))
        (self.site/'config.inc.php').write_text('<?php return json_decode('+payload+',true);')
    def value(self,key,**env):
        code='require '+json.dumps(str(self.site/'_app/src/bootstrap.php'))+'; echo json_encode(env_value('+json.dumps(key)+',"fallback"));'
        return json.loads(subprocess.check_output(['php','-r',code],env={**self.env,**env},text=True))
    def test_local_settings(self):
        self.config({'ZOD_DB_HOST':'172.16.1.121','ZOD_PUBLIC_URL':'https://zodee.retrocs.org'})
        self.assertEqual(self.value('ZOD_DB_HOST'),'172.16.1.121')
        self.assertEqual(self.value('ZOD_PUBLIC_URL'),'https://zodee.retrocs.org')
        self.assertEqual(self.value('ZOD_UNKNOWN'),'fallback')
    def test_environment_overrides_local_settings(self):
        self.config({'ZOD_DB_PASSWORD':'local-test-value'})
        self.assertEqual(self.value('ZOD_DB_PASSWORD',ZOD_DB_PASSWORD='environment-test-value'),'environment-test-value')
        self.assertEqual(self.value('ZOD_DB_PASSWORD',ZOD_DB_PASSWORD=''),'')
    def test_password_files_and_precedence(self):
        first=self.site/'first';first.write_text('local-file-value\n')
        second=self.site/'second';second.write_text('environment-file-value\n')
        self.config({'ZOD_DB_PASSWORD':'inline-test-value','ZOD_DB_PASSWORD_FILE':str(first)})
        self.assertEqual(self.value('ZOD_DB_PASSWORD'),'local-file-value')
        self.assertEqual(self.value('ZOD_DB_PASSWORD',ZOD_DB_PASSWORD='explicit-environment'),'explicit-environment')
        self.assertEqual(self.value('ZOD_DB_PASSWORD',ZOD_DB_PASSWORD='ignored',ZOD_DB_PASSWORD_FILE=str(second)),'environment-file-value')
    def test_missing_configuration_uses_environment_or_default(self):
        self.assertEqual(self.value('ZOD_DB_HOST'),'fallback')
        self.assertEqual(self.value('ZOD_DB_HOST',ZOD_DB_HOST='localhost'),'localhost')
if __name__=='__main__':unittest.main()
