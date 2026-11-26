"""Release package evidence must describe the files that are actually shipped."""
import hashlib,importlib.util,json,os,tempfile,unittest,sys
from unittest.mock import patch
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
notices=module('notices','packaging/notices.py');smoke=module('smoke','tools/package_smoke.py')
installation=module('installation','tools/installation_check.py')
class PackageTests(unittest.TestCase):
    def test_installation_receipt_is_bound_to_the_package_and_failed_rerun_removes_it(self):
        with tempfile.TemporaryDirectory() as temp:
            package=Path(temp)/'runtime';package.mkdir();binary=package/'game';binary.write_text('engine');driver=Path(temp)/'driver';driver.write_text('test driver')
            with patch.object(installation,'smoke'),patch.object(installation.subprocess,'run'):
                receipt=installation.check(package,'game',driver)
            proof=json.loads(receipt.read_text());self.assertTrue(proof['passed']);self.assertEqual(proof['files']['game'],hashlib.sha256(b'engine').hexdigest())
            with patch.object(installation,'smoke'),patch.object(installation.subprocess,'run',side_effect=subprocess.CalledProcessError(1,'suite')):
                with self.assertRaises(subprocess.CalledProcessError):installation.check(package,'game',driver)
            self.assertFalse(receipt.exists())
            receipt.write_text('old proof');driver.unlink()
            with self.assertRaisesRegex(RuntimeError,'regression'):installation.check(package,'game',driver)
            self.assertFalse(receipt.exists())
    def test_installation_check_rejects_package_mutation(self):
        with tempfile.TemporaryDirectory() as temp:
            package=Path(temp)/'runtime';package.mkdir();binary=package/'game';binary.write_text('engine');driver=Path(temp)/'driver';driver.write_text('driver')
            with patch.object(installation,'smoke'),patch.object(installation.subprocess,'run',side_effect=lambda *a,**k:binary.write_text('modified')):
                with self.assertRaisesRegex(RuntimeError,'modified staged package'):installation.check(package,'game',driver)
            self.assertFalse((package.parent/'INSTALL-CHECK-runtime.json').exists())

    def test_inventory_hashes_actual_dependencies_and_includes_license_texts(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);package=root/'zod-enhanced';package.mkdir();binary=package/'codec.dll';binary.write_bytes(b'packaged dependency')
            dependency=root/'Cellar/codec/1.2';(dependency/'lib').mkdir(parents=True);source=dependency/'lib/codec.dll';source.write_bytes(b'original dependency');(dependency/'LICENSE').write_text('Example license text')
            count,licenses=notices.inventory(package,package/'ThirdParty',[source]);self.assertEqual((count,licenses),(1,1))
            bom=json.loads((package/'ThirdParty/bom.cdx.json').read_text());self.assertEqual(bom['components'][0]['hashes'][0]['content'],hashlib.sha256(binary.read_bytes()).hexdigest())
            self.assertTrue(any('Example license text' in p.read_text() for p in (package/'ThirdParty/licenses').iterdir()))
    def test_smoke_refuses_a_binary_outside_the_package(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);package=root/'zod-enhanced';package.mkdir();outside=root/'other';outside.write_text('not the packaged binary')
            with self.assertRaises(RuntimeError):smoke.smoke(package,'../other')
    @unittest.skipIf(os.name=='nt','Uses a POSIX executable fixture')
    def test_smoke_runs_the_package_in_an_isolated_working_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            package=Path(temp)/'zod-enhanced';package.mkdir();exe=package/'zod';exe.write_text('#!/bin/sh\ncase "$1" in\n--version) printf "zod-enhanced fixture\\n";;\n--check-content) printf "Original levels: 0/1\\n"; exit 2;;\n--diagnostics) mkdir -p "$ZOD_PROFILE_DIR"; echo safe > "$ZOD_PROFILE_DIR/report.txt"; echo "$ZOD_PROFILE_DIR/report.txt";;\n*) exit 1;;\nesac\n');exe.chmod(0o755)
            smoke.smoke(package,'zod');self.assertTrue(json.loads((package.parent/'SMOKE-zod-enhanced.json').read_text())['passed'])
if __name__=='__main__':unittest.main()
