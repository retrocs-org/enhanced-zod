"""Exercise make clean in isolated checkouts; never clean the working build."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class CleanBuildTests(unittest.TestCase):
    def test_cleanup_preserves_media_state_scripts_and_external_symlink_targets(self):
        with tempfile.TemporaryDirectory() as directory:
            base = pathlib.Path(directory)
            repo = base / 'repo'
            (repo / 'tools').mkdir(parents=True)
            shutil.copy2(ROOT / 'Makefile', repo / 'Makefile')
            shutil.copy2(ROOT / 'tools/clean_builds.py', repo / 'tools/clean_builds.py')
            (repo / 'CMakeLists.txt').write_text('project(zod CXX)\n')
            generated = ['build-app/game', 'build-app.backup-old/game',
                         'build-importer-venv/python', 'build-asset-helper/helper',
                         'tools/__pycache__/module.pyc', 'src/legacy.o', 'CMakeFiles/cache',
                         'orchestrator/orchestrator']
            preserved = ['build-wasm.sh', 'build-custom/notes',
                         'build-asset-cleanup/backup/assets/reference.png',
                         'assets/original-z/manifest.json', 'assets/soundfonts/bank.sf2',
                         'build-private-state/credentials', 'build-public-state/server.json',
                         'build-linux-vm/disk.img', '.lima/config.yaml']
            for name in generated + preserved:
                path = repo / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('keep or remove')
            external = base / 'external'
            external.mkdir()
            (external / 'valuable').write_text('keep')
            (repo / 'build-windows').symlink_to(external, target_is_directory=True)
            before = sorted(str(p.relative_to(repo)) for p in repo.rglob('*'))
            subprocess.run(['make', 'clean-dry-run'], cwd=repo, check=True, capture_output=True)
            self.assertEqual(before, sorted(str(p.relative_to(repo)) for p in repo.rglob('*')))
            subprocess.run(['make', 'clean'], cwd=repo, check=True, capture_output=True)
            self.assertTrue(all(not (repo / name).exists() for name in generated))
            self.assertTrue(all((repo / name).exists() for name in preserved))
            self.assertFalse((repo / 'build-windows').is_symlink())
            self.assertEqual((external / 'valuable').read_text(), 'keep')
            # Cleaning an already clean checkout succeeds too.
            subprocess.run(['make', 'clean'], cwd=repo, check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
