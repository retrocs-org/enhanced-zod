"""Build IDs distinguish content/configuration and survive source archives."""
import importlib.util,json,plistlib,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('build_metadata',ROOT/'tools/build_metadata.py');m=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(m)
class BuildMetadataTests(unittest.TestCase):
 def test_deterministic_content_identity_and_archive_provenance(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)/'source';(root/'src').mkdir(parents=True);(root/'tools').mkdir();(root/'VERSION').write_text('1.2.3\n');(root/'CMakeLists.txt').write_text('project(test)');(root/'src/main.cpp').write_text('first');(root/'tools/build_metadata.py').write_text('metadata tool')
   (root/'BUILD-PROVENANCE.json').write_text(json.dumps({'commit':'a'*40}));first=m.metadata(root);self.assertEqual(first,m.metadata(root));self.assertEqual(first['commit'],'a'*40)
   (root/'src/main.cpp').write_bytes(b'first\r\n');canonical=m.metadata(root)['source_sha256'];(root/'src/main.cpp').write_bytes(b'first\n');self.assertEqual(canonical,m.metadata(root)['source_sha256'])
   (root/'src/main.cpp').write_text('second');self.assertNotEqual(first['source_sha256'],m.metadata(root)['source_sha256'])
   out=Path(tmp)/'generated';plist=Path(tmp)/'Info.plist';plist.write_bytes(plistlib.dumps({'CFBundleVersion':'old'}));cmd=[sys.executable,str(ROOT/'tools/build_metadata.py'),'--root',str(root),'--output',str(out),'--config','Debug','--platform','Test-arm64','--compiler','Test-1','--plist',str(plist)]
   subprocess.run(cmd,check=True);identity=json.loads((out/'BUILD-IDENTITY.json').read_text());self.assertTrue(identity['build_id'].endswith('.Debug'));self.assertEqual(identity['platform'],'Test-arm64');self.assertEqual(plistlib.loads(plist.read_bytes())['ZodBuildID'],identity['build_id']);stamp=(out/'zbuild_metadata.h').stat().st_mtime_ns;subprocess.run(cmd,check=True);self.assertEqual(stamp,(out/'zbuild_metadata.h').stat().st_mtime_ns)
   (root/'src/main.cpp').write_text('third');subprocess.run(cmd+['--stamp-only'],check=True);self.assertEqual(plistlib.loads(plist.read_bytes())['ZodBuildID'],identity['build_id']);self.assertEqual(stamp,(out/'zbuild_metadata.h').stat().st_mtime_ns)
if __name__=='__main__':unittest.main()
