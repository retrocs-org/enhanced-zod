"""Release package evidence must describe the files that are actually shipped."""
import hashlib,importlib.util,json,os,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
notices=module('notices','packaging/notices.py');smoke=module('smoke','tools/package_smoke.py')
class PackageTests(unittest.TestCase):
    def test_inventory_hashes_actual_dependencies_and_includes_license_texts(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);package=root/'zod-ee';package.mkdir();binary=package/'codec.dll';binary.write_bytes(b'packaged dependency')
            dependency=root/'Cellar/codec/1.2';(dependency/'lib').mkdir(parents=True);source=dependency/'lib/codec.dll';source.write_bytes(b'original dependency');(dependency/'LICENSE').write_text('Example license text')
            count,licenses=notices.inventory(package,package/'ThirdParty',[source]);self.assertEqual((count,licenses),(1,1))
            bom=json.loads((package/'ThirdParty/bom.cdx.json').read_text());self.assertEqual(bom['components'][0]['hashes'][0]['content'],hashlib.sha256(binary.read_bytes()).hexdigest())
            self.assertTrue(any('Example license text' in p.read_text() for p in (package/'ThirdParty/licenses').iterdir()))
    def test_smoke_refuses_a_binary_outside_the_package(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);package=root/'zod-ee';package.mkdir();outside=root/'other';outside.write_text('not the packaged binary')
            with self.assertRaises(RuntimeError):smoke.smoke(package,'../other')
    @unittest.skipIf(os.name=='nt','Uses a POSIX executable fixture')
    def test_smoke_runs_the_package_in_an_isolated_working_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            package=Path(temp)/'zod-ee';package.mkdir();exe=package/'zod';exe.write_text('#!/bin/sh\ntest "$1" = "--version" || exit 1\nprintf "zod-ee fixture\\n"\n');exe.chmod(0o755)
            smoke.smoke(package,'zod');self.assertTrue(json.loads((package.parent/'SMOKE-zod-ee.json').read_text())['passed'])
if __name__=='__main__':unittest.main()
