"""Exercise source selection, archives, provenance and offline publication guards."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile
SPEC=importlib.util.spec_from_file_location('release',Path(__file__).resolve().parents[1]/'tools/release.py')
r=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(r)
class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name).resolve()
        for name,text in {'VERSION':'1.2.3\n','README.md':'zod-enhanced','LICENSE':'GPLv3','src/main.cpp':'source','packaging/macos/build-app.sh':'#!/bin/sh\nexit 0\n','public-web/index.php':'<?php echo "zod-enhanced";','server-agent/go.mod':'module zod-server-agent\n','docs/NETWORK-SERVICES.md':'Hosting and configuration guide.','docs/RELEASE-NOTES.md':'Release notes describing the features in this version.','.gitignore':'outputs/\nbuild/\nwork/\n'}.items():
            file=self.root/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_text(text)
        subprocess.run(['git','init','-q',str(self.root)],check=True)
        for args in [('config','user.email','tests@example.invalid'),('config','user.name','Release tests'),('add','.'),('commit','-qm','fixture')]:subprocess.run(['git','-C',str(self.root),*args],check=True)
        self.out=self.root/'outputs/release-test'
    def prepare(self,artifacts=None,dirty=False):return r.prepare(self.root,self.out,artifacts or [],dirty)
    def test_prepare_source_and_website_with_checksums(self):
        manifest=self.prepare();r.verify(self.root,self.out)
        self.assertEqual(manifest['version'],'1.2.3')
        with zipfile.ZipFile(self.out/'zod-enhanced-1.2.3-source.zip') as archive:
            self.assertIn('zod-enhanced-1.2.3/src/main.cpp',archive.namelist())
            self.assertIn('zod-enhanced-1.2.3/packaging/macos/build-app.sh',archive.namelist())
            self.assertIn('zod-enhanced-1.2.3/public-web/index.php',archive.namelist())
            self.assertFalse(any('/.git/' in n for n in archive.namelist()))
        with zipfile.ZipFile(self.out/'zod-enhanced-1.2.3-web.zip') as archive:
            self.assertIn('zod-enhanced-web/public-web/index.php',archive.namelist())
            self.assertIn('zod-enhanced-web/docs/NETWORK-SERVICES.md',archive.namelist())
            self.assertFalse(any('/server-agent/' in n for n in archive.namelist()))
        self.assertFalse((self.out/'receipt-github.json').exists())
    def test_previous_project_name_remains_verifiable(self):
        manifest=self.prepare();manifest['project']='zod-ee'
        (self.out/'release-manifest.json').write_text(json.dumps(manifest))
        self.assertEqual(r.verify(self.root,self.out)['project'],'zod-ee')
    def test_archive_tampering_fails_verification(self):
        m=self.prepare();file=self.out/next(iter(m['artifacts']));file.write_bytes(file.read_bytes()+b'changed')
        with self.assertRaises(r.ReleaseError):r.verify(self.root,self.out)
    def test_changed_source_invalidates_prepared_release(self):
        self.prepare();(self.root/'src/main.cpp').write_text('new source')
        with self.assertRaises(r.ReleaseError):r.verify(self.root,self.out)
    def test_dirty_preview_cannot_publish(self):
        (self.root/'src/main.cpp').write_text('uncommitted')
        with self.assertRaises(r.ReleaseError):self.prepare()
        self.prepare(dirty=True)
        with self.assertRaises(r.ReleaseError):r.publish(self.root,self.out,{},'github',True)
    def test_private_configuration_and_source_links_rejected(self):
        (self.root/'.env').write_text('SECRET=not-a-real-secret')
        with self.assertRaises(r.ReleaseError):r.sources(self.root)
        (self.root/'.env').unlink();(self.root/'linked.cpp').symlink_to(self.root/'src/main.cpp')
        with self.assertRaises(r.ReleaseError):r.sources(self.root)
    def test_output_must_stay_in_release_area(self):
        with self.assertRaises(r.ReleaseError):r.output_dir(self.root,self.root/'src')
    def test_no_publication_without_explicit_confirmation(self):
        self.prepare()
        with self.assertRaises(r.ReleaseError):r.publish(self.root,self.out,{},'github',False)
    def test_sidecar_and_notes_tampering_rejected(self):
        self.prepare();(self.out/'RELEASE-NOTES.md').write_text('changed')
        with self.assertRaises(r.ReleaseError):r.verify(self.root,self.out)
    def test_runtime_requires_fresh_binary_provenance(self):
        binary=self.root/'build/zod-enhanced.app/Contents/MacOS/zod-enhanced';binary.parent.mkdir(parents=True);binary.write_bytes(b'binary')
        r.record_build(self.root,self.root/'build',[binary],True)
        self.prepare([('macos-arm64',binary.parents[2])]);r.verify(self.root,self.out)
        evidence=json.loads((self.out/'release-manifest.json').read_text())
        self.assertTrue(evidence['runtime_provenance']['macos-arm64']['tests_passed'])
    def test_runtime_escape_link_rejected(self):
        directory=self.root/'build/runtime';directory.mkdir(parents=True);(directory/'escape').symlink_to(self.root/'LICENSE')
        with self.assertRaises(r.ReleaseError):self.prepare([('linux-x86_64',directory)],True)
    def test_download_metadata_tampering_rejected(self):
        self.prepare();(self.out/'downloads.json').write_text('{"windows":{"url":"changed"}}')
        with self.assertRaises(r.ReleaseError):r.verify(self.root,self.out)
    def test_failed_prepare_leaves_no_partial_release(self):
        with self.assertRaises(r.ReleaseError):self.prepare([('wrong-platform','missing')])
        self.assertFalse(self.out.exists())
    def test_runtime_must_contain_verified_binary(self):
        binary=self.root/'build/zod-enhanced';binary.parent.mkdir();binary.write_bytes(b'verified')
        runtime=self.root/'build/runtime';runtime.mkdir();(runtime/'zod').write_bytes(b'old binary')
        r.record_build(self.root,self.root/'build',[binary],True)
        with self.assertRaises(r.ReleaseError):self.prepare([('linux-x86_64',runtime)])
        self.assertFalse(self.out.exists())
    def test_collect_matching_platform_releases(self):
        self.prepare()
        with zipfile.ZipFile(self.out/'zod-enhanced-1.2.3-source.zip') as archive:
            self.assertEqual(archive.getinfo('zod-enhanced-1.2.3/BUILD-PROVENANCE.json').date_time,(2020,1,1,0,0,0))
        second=self.root/'outputs/second';r.prepare(self.root,second,[])
        combined=self.root/'outputs/combined';r.collect(self.root,combined,[self.out,second])
        self.assertEqual(r.verify(self.root,combined)['version'],'1.2.3')
    def test_collect_rejects_changed_snapshot(self):
        self.prepare();(self.root/'src/main.cpp').write_text('changed')
        second=self.root/'outputs/second';r.prepare(self.root,second,[],True)
        combined=self.root/'outputs/combined'
        with self.assertRaises(r.ReleaseError):r.collect(self.root,combined,[self.out,second])
        self.assertFalse(combined.exists())
    def config(self):return {'github':'https://github.com/retrocs-org/zodee','gitlab':'https://gitlab.kladhest.se/retrocs-org/zodee','source_branch':'main'}
    def test_self_hosted_gitlab_and_ssh_destination(self):
        cfg=self.config();cfg['gitlab_git']='git@gitlab.kladhest.se:retrocs-org/zodee.git'
        remote=r.destination(cfg,'gitlab');self.assertEqual(remote['cli_repo'],'gitlab.kladhest.se/retrocs-org/zodee');self.assertEqual(remote['push_url'],cfg['gitlab_git'])
        cfg['gitlab_git']='git@other.invalid:retrocs-org/zodee.git'
        with self.assertRaises(r.ReleaseError):r.destination(cfg,'gitlab')
    def test_source_requires_confirmation_and_clean_tree(self):
        with self.assertRaises(r.ReleaseError):r.push_source(self.root,self.config(),['gitlab'])
        (self.root/'README.md').write_text('dirty')
        with self.assertRaises(r.ReleaseError):r.push_source(self.root,self.config(),['gitlab'],dry_run=True)
    def test_two_repo_source_preflight_never_pushes(self):
        calls=[];original=subprocess.run
        def run(command,**kwargs):
            if 'push' in command:calls.append(command);return subprocess.CompletedProcess(command,0)
            return original(command,**kwargs)
        with patch.object(r.subprocess,'run',side_effect=run):result=r.push_source(self.root,self.config(),['gitlab','github'],dry_run=True)
        self.assertFalse(result['uploaded']);self.assertEqual(len(calls),2);self.assertTrue(all('--dry-run' in c and '--no-follow-tags' in c for c in calls));self.assertFalse((self.root/'outputs/source').exists())
        self.assertIn('https://gitlab.kladhest.se/retrocs-org/zodee.git',calls[0])
    def test_preflight_failure_prevents_both_uploads(self):
        calls=[];original=subprocess.run
        def run(command,**kwargs):
            if 'push' in command:
                calls.append(command)
                if len(calls)==2:raise subprocess.CalledProcessError(1,command)
                return subprocess.CompletedProcess(command,0)
            return original(command,**kwargs)
        with patch.object(r.subprocess,'run',side_effect=run):
            with self.assertRaises(subprocess.CalledProcessError):r.push_source(self.root,self.config(),['gitlab','github'],confirmed=True)
        self.assertEqual(len(calls),2);self.assertTrue(all('--dry-run' in c for c in calls))
    def test_source_pushes_exact_commit_and_verifies_each_destination(self):
        sha=r.git(self.root,'rev-parse','HEAD');calls=[];original_run=subprocess.run;original_git=r.git
        def run(command,**kwargs):
            if 'push' in command:calls.append(command);return subprocess.CompletedProcess(command,0)
            return original_run(command,**kwargs)
        def git(root,*args):
            if args[0]=='ls-remote':return sha+'\trefs/heads/main'
            return original_git(root,*args)
        with patch.object(r.subprocess,'run',side_effect=run),patch.object(r,'git',side_effect=git):result=r.push_source(self.root,self.config(),['gitlab','github'],confirmed=True)
        self.assertTrue(result['uploaded']);self.assertEqual(len(calls),4);self.assertTrue(all(c[-1]==sha+':refs/heads/main' and '--force' not in c for c in calls))
        for target in ['gitlab','github']:self.assertTrue(json.loads((self.root/'outputs/source'/('receipt-'+target+'.json')).read_text())['verified'])
    def test_source_preflight_timeout_writes_no_receipt(self):
        original=subprocess.run
        def run(command,**kwargs):
            if 'push' in command:
                self.assertEqual(kwargs.get('timeout'),45)
                raise subprocess.TimeoutExpired(command,45)
            return original(command,**kwargs)
        with patch.object(r.subprocess,'run',side_effect=run):
            with self.assertRaises(subprocess.TimeoutExpired):r.push_source(self.root,self.config(),['github'],confirmed=True)
        self.assertFalse((self.root/'outputs/source').exists())

    def test_source_rejects_committed_token(self):
        (self.root/'leaked.txt').write_text('glpat-'+'a'*25)
        subprocess.run(['git','-C',str(self.root),'add','.'],check=True);subprocess.run(['git','-C',str(self.root),'commit','-qm','unsafe fixture'],check=True)
        with self.assertRaises(r.ReleaseError):r.push_source(self.root,self.config(),['gitlab'],dry_run=True)
    def test_duplicate_or_unknown_targets_rejected(self):
        for value in ['github,github','unknown','']:
            with self.assertRaises(r.ReleaseError):r.selected_targets(value)

    def test_source_archives_exclude_game_media_and_font_binaries(self):
        for name in ['assets/sprite.png','maps/original.map','tools/z_graphics_patches/patch.png','public-web/assets/heading.ttf']:
            file=self.root/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(b'local media')
        files=r.sources(self.root);self.assertFalse(any(r.source_policy.forbidden(name) for name in files))
    def test_deleted_media_in_history_still_blocks_push(self):
        file=self.root/'old.png';file.write_bytes(b'old media')
        subprocess.run(['git','-C',str(self.root),'add','.'],check=True);subprocess.run(['git','-C',str(self.root),'commit','-qm','old media'],check=True)
        file.unlink();subprocess.run(['git','-C',str(self.root),'add','-u'],check=True);subprocess.run(['git','-C',str(self.root),'commit','-qm','remove media'],check=True)
        with self.assertRaises(r.ReleaseError):r.push_source(self.root,self.config(),['gitlab'],dry_run=True)
        self.assertIn('old.png',r.source_policy.history_paths(self.root))

    def test_upstream_is_not_publication_destination(self):
        config=self.root/'work/config.json';config.parent.mkdir();config.write_text('{"github_repo":"fenio/zod"}')
        with self.assertRaises(r.ReleaseError):r.configured(config)
if __name__=='__main__':unittest.main()
