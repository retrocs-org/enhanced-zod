"""Exercise self-contained website staging, lint failures and rollback."""
import os,pathlib,shutil,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
PHP=shutil.which('php')
@unittest.skipUnless(PHP,'PHP CLI required')
class Refresh(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='zodee-refresh-test-');self.addCleanup(self.temporary.cleanup)
        self.root=pathlib.Path(self.temporary.name);self.repo=self.root/'repository';self.repo.mkdir();self.parent=self.root/'vhosts';self.parent.mkdir();self.site=self.parent/'zodee.retrocs.org'
        self.bin=self.root/'bin';self.bin.mkdir()
        for name,body in {'id':'echo 0','getent':'exit 0','chown':'exit 0','mv':'case "$1" in */new-site) [ "${FAIL_SITE_MOVE:-0}" = 0 ] || exit 1;; esac\nexec /bin/mv "$@"'}.items():
            path=self.bin/name;path.write_text('#!/bin/sh\n'+body+'\n');path.chmod(0o755)
        self.env=dict(os.environ,PATH=str(self.bin)+os.pathsep+os.environ['PATH'],ZODEE_DEPLOY_PARENT=str(self.parent),ZODEE_REPOSITORY=str(self.repo))
        subprocess.run(['git','init','-q','-b','main',str(self.repo)],check=True)
        app=self.repo
        for name,text in {'public-web/index.php':"<?php require __DIR__.'/_app/src/service.php'; echo refresh_fixture();",'public-web/assets/site.css':'body{}','public-web/_app/src/service.php':"<?php function refresh_fixture(){return 'working-private-app';}",'public-web/_app/src/bootstrap.php':'<?php','public-web/_app/src/views.php':'<?php','public-web/_app/bin/migrate.php':'<?php'}.items():
            file=app/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_text(text)
        self.commit()
    def commit(self):
        subprocess.run(['git','-C',str(self.repo),'add','.'],check=True)
        subprocess.run(['git','-C',str(self.repo),'-c','user.name=Test','-c','user.email=test@example.invalid','-c','commit.gpgsign=false','commit','-qm','fixture'],check=True)
    def deploy(self,success=True,**extra):
        result=subprocess.run(['sh',str(ROOT/'packaging/linux/refresh-zodee.sh')],env=dict(self.env,**extra),text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.assertEqual(result.returncode==0,success,result.stdout+result.stderr);return result
    def test_self_contained_application_and_repeated_refresh(self):
        self.deploy();self.assertEqual(subprocess.check_output([PHP,str(self.site/'index.php')],text=True),'working-private-app')
        self.assertFalse((self.site/'src').exists());self.assertFalse((self.site/'bin').exists());self.assertTrue((self.site/'assets/site.css').is_file())
        first=(self.site/'index.php').read_text();self.deploy();self.assertEqual(first,(self.site/'index.php').read_text())
        self.assertEqual(first,(self.repo/'public-web/index.php').read_text())
        self.assertTrue((self.site/'_app/src/service.php').is_file())
        self.assertFalse((self.parent/'.zodee-application').exists())
        self.assertEqual(subprocess.check_output([PHP,str(self.site/'index.php')],text=True),'working-private-app')
    def test_preserves_local_config(self):
        self.deploy()
        config="<?php return ['ZOD_DB_HOST'=>'172.16.1.121','ZOD_DB_PASSWORD'=>'test-only-local-secret'];"
        (self.site/'config.inc.php').write_text(config)
        (self.repo/'public-web/config.inc.php').write_text("<?php return [];")
        self.commit();self.deploy()
        self.assertEqual((self.site/'config.inc.php').read_text(),config)
    def test_invalid_local_config_keeps_site(self):
        self.deploy();previous=(self.site/'index.php').read_text()
        (self.site/'config.inc.php').write_text('<?php broken syntax here;')
        self.deploy(False)
        self.assertEqual((self.site/'index.php').read_text(),previous)
    def test_failed_lint_keeps_site(self):
        self.site.mkdir();(self.site/'index.php').write_text('previous')
        (self.repo/'public-web/_app/src/service.php').write_text('<?php this is invalid php ;');self.commit();self.deploy(False)
        self.assertEqual((self.site/'index.php').read_text(),'previous')
    def test_missing_php_opening_tag_keeps_site(self):
        self.site.mkdir();(self.site/'index.php').write_text('previous')
        (self.repo/'public-web/index.php').write_text("declare(strict_types=1); echo 'broken';")
        self.commit();result=self.deploy(False)
        self.assertIn('Missing PHP opening tag',result.stderr)
        self.assertEqual((self.site/'index.php').read_text(),'previous')
    def test_missing_entry_keeps_site(self):
        self.site.mkdir();(self.site/'index.php').write_text('previous');(self.repo/'public-web/index.php').unlink();self.commit();self.deploy(False)
        self.assertEqual((self.site/'index.php').read_text(),'previous')
    def test_switch_failure_restores_previous(self):
        self.deploy();previous=(self.site/'index.php').read_text();self.deploy(False,FAIL_SITE_MOVE='1')
        self.assertEqual((self.site/'index.php').read_text(),previous);self.assertEqual(subprocess.check_output([PHP,str(self.site/'index.php')],text=True),'working-private-app')
    def test_refuses_symlink_document_root(self):
        elsewhere=self.root/'elsewhere';elsewhere.mkdir();self.site.symlink_to(elsewhere,target_is_directory=True);self.deploy(False);self.assertEqual(list(elsewhere.iterdir()),[])
if __name__=='__main__':unittest.main()
