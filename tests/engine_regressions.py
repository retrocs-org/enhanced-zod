"""Real engine lifecycle tests. All saves, progress and temp maps are isolated."""
import contextlib
import json
import os
import pathlib
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
import threading
import unittest

GAME, DRIVER, ROOT = map(str, sys.argv[1:4])
installation_check='--installation-check' in sys.argv[4:]
sys.argv = sys.argv[:1]
if installation_check:
    sys.argv += ['EngineRegressions.'+name for name in [
        'test_clean_checkout_can_render_import_screen_without_game_media',
        'test_game_routes_clean_profile_to_import_before_server_startup',
        'test_fresh_profile_imported_authored_level_can_start_save_and_reload',
        'test_complete_two_player_match_startup',
        'test_direct_connection_adopts_real_socket_and_reports_refusal',
        'test_version_preflight_rejects_incompatible_or_silent_servers',
        'test_private_multiplayer_reconnects_and_cancel_stops_retries',
        'test_malformed_and_fragmented_packet_decoder',
        'test_embedded_server_transfers_maps_on_small_worker_stack',
    ]]
WORKSPACE=ROOT
_fixture_workspace=None
if not (pathlib.Path(ROOT)/'maps/p02_bb_orig01.map').is_file() or os.environ.get('ZOD_TEST_SOURCE_ONLY')=='1':
    import source_fixtures
    _fixture_workspace=tempfile.TemporaryDirectory(prefix='zodee-authored-fixtures-')
    WORKSPACE=_fixture_workspace.name;source_fixtures.workspace(ROOT,WORKSPACE)
MAP = str(pathlib.Path(WORKSPACE) / 'maps/p02_bb_orig01.map')
PROTOCOL = json.loads(subprocess.check_output([DRIVER, 'protocol'], text=True))

class Client:
    def __init__(self, connection):
        self.connection = connection
        connection.settimeout(5)

    def send(self, name, body=b''):
        self.connection.sendall(struct.pack('=ii', len(body), PROTOCOL[name]) + body)

    def exact(self, count):
        parts = bytearray()
        while len(parts) < count:
            data = self.connection.recv(count - len(parts))
            if not data:
                raise AssertionError('server closed connection during handshake')
            parts.extend(data)
        return bytes(parts)

    def packet(self):
        size, kind = struct.unpack('=ii', self.exact(8))
        if not 0 <= size <= 400000 - 8:
            raise AssertionError(f'invalid server packet size: {size}')
        return kind, self.exact(size)

    def until(self, name):
        deadline = time.monotonic() + 8
        received = []
        while time.monotonic() < deadline:
            kind, body = self.packet()
            received.append((kind, body))
            if kind == PROTOCOL[name]:
                return body, received
        raise AssertionError(f'timed out waiting for {name}')

class EngineRegressions(unittest.TestCase):
    def test_map_integrity_sha256_and_transfer_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            result=subprocess.check_output([DRIVER,'integrity'],cwd=WORKSPACE,env=self.environment(directory),text=True,timeout=20)
            self.assertIn('map-integrity-ok',result)

    def test_command_menu_keeps_actions_connected(self):
        with tempfile.TemporaryDirectory(prefix='zod-disc-menu-') as directory:
            root=pathlib.Path(directory);assets=root/'imports/disc-fixture/assets'
            (assets/'frontend/disc').mkdir(parents=True)
            (assets/'original-z').mkdir();(assets/'original-z/manifest.json').write_text('{}')
            (root/'import-active.txt').write_text('disc-fixture')
            env=self.environment(directory);env['ZOD_PROFILE_DIR']=directory
            image=root/'disc-menu.png'
            result=subprocess.check_output([DRIVER,'disc-menu',str(image)],cwd=WORKSPACE,env=env,text=True)
            self.assertIn('disc-menu-actions-ok',result)
            self.assertEqual(image.read_bytes()[:8],b'\x89PNG\r\n\x1a\n')

    def test_only_active_menu_renders_and_receives_input(self):
        with tempfile.TemporaryDirectory(prefix='zod-menu-navigation-') as directory:
            result=subprocess.check_output([DRIVER,'menu-navigation'],cwd=WORKSPACE,env=self.environment(directory),text=True)
            self.assertIn('menu-navigation-ok',result)

    def test_separate_campaigns_progress_safe_paths_and_independent_audio_sliders(self):
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-campaign-') as directory:
            env=self.environment(directory);env['ZOD_PROFILE_DIR']=directory
            result=subprocess.check_output([DRIVER,'campaign-audio',directory],cwd=WORKSPACE,env=env,text=True)
            self.assertIn('campaign-audio-ok',result)

    def test_map_preview_uses_imported_palette(self):
        with tempfile.TemporaryDirectory(prefix='zod-map-preview-') as directory:
            root=pathlib.Path(directory);assets=root/'imports/preview-fixture/assets'
            (assets/'planets').mkdir(parents=True)
            (assets/'original-z').mkdir();(assets/'original-z/manifest.json').write_text('{}')
            (root/'import-active.txt').write_text('preview-fixture')
            env=self.environment(directory);env['ZOD_PROFILE_DIR']=directory
            result=subprocess.check_output([DRIVER,'preview-palette'],cwd=WORKSPACE,env=env,text=True)
            self.assertIn('imported-preview-ok',result)

    def test_stress_assessment_rejects_inactive_and_growing_runs(self):
        sys.path.insert(0,str(pathlib.Path(ROOT)/'tools'))
        import stress
        good=dict(ticks=600,reloads=6,moved=10,damaged=5,dead=1,live_units=2,survivors=2,warm_rss=1024,final_rss=2048,peak_rss=2048)
        self.assertFalse(stress.assess_metrics(good,600,32))
        for key,value in [('moved',0),('damaged',0),('reloads',0),('live_units',0),('survivors',0),('peak_rss',100*1024*1024),('warm_rss',None)]:
            bad=dict(good);bad[key]=value
            self.assertTrue(stress.assess_metrics(bad,600,32),key)

    def test_campaign_script_replay_and_presets(self):
        for mode,arg in [("replay-feedback",None),("authoring-tools",None),("feature-checks",None),("script-save",MAP),("stress",MAP)]:
            with tempfile.TemporaryDirectory(prefix="zodee-features-") as directory:
                command=[DRIVER,mode,arg or directory]
                if mode=="stress":command += ["120","64"]
                result=subprocess.check_output(command,cwd=WORKSPACE,env=self.environment(directory),text=True,timeout=60)
                self.assertIn({"replay-feedback":"replay-feedback-ok","authoring-tools":"authoring-tools-ok","feature-checks":"feature-checks-ok","script-save":"script-save-ok","stress":"stress-ok"}[mode],result)

    def test_objective_hud_packets_and_summary_return_before_continue(self):
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-objective-ui-') as directory:
            result=subprocess.check_output([DRIVER,'objectives-summary'],cwd=WORKSPACE,env=self.environment(directory),text=True)
            self.assertIn('objectives-summary-ok',result)

    def test_playtest_return_terrain_tools_records_and_retry(self):
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-enhance-') as directory:
            result=subprocess.check_output([DRIVER,'enhancements',directory],cwd=WORKSPACE,env=self.environment(directory),text=True)
            self.assertIn('enhancements-ok',result)

    def test_portable_video_subtitles_audio_volume_and_skip(self):
        import shutil
        ffmpeg=shutil.which('ffmpeg')
        if not ffmpeg:self.skipTest('FFmpeg not installed on this runner')
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-movie-') as directory:
            movie=pathlib.Path(directory)/'fixture.mp4'
            subprocess.run([ffmpeg,'-nostdin','-v','error','-f','lavfi','-i','color=c=blue:s=160x100:r=30','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','0.5','-c:v','mpeg4','-c:a','aac',str(movie)],check=True,timeout=15)
            movie.with_suffix('.srt').write_text('1\n00:00:00,000 --> 00:00:00,400\nTest subtitle\n\n')
            result=subprocess.run([DRIVER,'movie',str(movie)],cwd=WORKSPACE,env=self.environment(directory),capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('movie-ok',result.stdout)

    def test_campaign_continue_reads_unlocks_before_any_map_is_loaded(self):
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-resume-') as directory:
            result=subprocess.check_output([DRIVER,'campaign-menu-resume'],cwd=WORKSPACE,env=self.environment(directory),text=True)
            self.assertIn('campaign-menu-resume-ok',result)

    def test_objective_server_config_and_save_roundtrip(self):
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-objectives-') as directory:
            result=subprocess.check_output([DRIVER,'objective-server'],cwd=WORKSPACE,env=self.environment(directory),text=True)
            self.assertIn('objective-server-ok',result)

    def test_campaign_pack_roundtrip_validation_objectives_and_category_defaults(self):
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-content-') as directory:
            result=subprocess.check_output([DRIVER,'content-polish',directory],cwd=WORKSPACE,env=self.environment(directory),text=True)
            self.assertIn('content-polish-ok',result)

    def test_standard_deflated_campaign_zip_is_accepted(self):
        import zipfile
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-deflate-') as directory:
            archive=pathlib.Path(directory)/'campaign.zip'
            with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as pack:
                pack.writestr('example/campaign.ini','format=1\nid=example\ntitle=Example\n[mission]\nmap=one.map\n')
                pack.write(pathlib.Path(WORKSPACE)/'maps/p02_bb_orig01.map','example/one.map')
            result=subprocess.run([DRIVER,'inspect-pack',str(archive)],cwd=WORKSPACE,env=self.environment(directory),text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('pack-ok',result.stdout)

    def test_campaign_zip_rejects_traversal_and_duplicate_entries(self):
        import zipfile
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-pack-') as directory:
            for entries in [[('../escape.txt','bad')],[('campaign.ini','bad'),('Campaign.ini','duplicate')],[('/absolute.txt','bad')]]:
                archive=pathlib.Path(directory)/'bad.zip'
                with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as pack:
                    for name,data in entries:pack.writestr(name,data)
                result=subprocess.run([DRIVER,'inspect-pack',str(archive)],cwd=WORKSPACE,env=self.environment(directory),text=True,capture_output=True)
                self.assertEqual(result.returncode,2,result.stdout+result.stderr)
                self.assertFalse((pathlib.Path(directory).parent/'escape.txt').exists())

    def test_workspace_edits_undo_save_copy_and_unsaved_confirmation(self):
        with tempfile.TemporaryDirectory(prefix='zod-workspace-') as directory:
            image=pathlib.Path(directory)/'workspace.png'
            result=subprocess.check_output([DRIVER,'workspace',str(image)],cwd=WORKSPACE,env=self.environment(directory),text=True)
            self.assertIn('workspace-ok',result)
            self.assertEqual(image.read_bytes()[:8],b'\x89PNG\r\n\x1a\n')

    def test_failed_deployment_can_retry_reconnection(self):
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-retry-') as directory, socket.socket() as unavailable:
            unavailable.bind(('127.0.0.1', 0))
            result=subprocess.check_output([DRIVER,'reconnect-failure',str(unavailable.getsockname()[1])],cwd=WORKSPACE,env=self.environment(directory),text=True,timeout=20)
            self.assertIn('reconnect-failure-ok',result)

    def test_campaign_playtest_progression_editor_controller_accessibility_and_profiling(self):
        with tempfile.TemporaryDirectory(prefix='zodee-features-') as directory:
            result=subprocess.run([DRIVER,'next-features',directory],cwd=WORKSPACE,env=self.environment(directory),text=True,capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('next-features-ok',result.stdout)

    def test_mixed_speed_units_move_and_unreachable_orders_report_to_the_owner(self):
        with tempfile.TemporaryDirectory(prefix='zodee-movement-') as directory:
            result=subprocess.run([DRIVER,'movement',MAP],cwd=WORKSPACE,env=self.environment(directory),text=True,capture_output=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('movement-ok',result.stdout)

    def test_rotating_save_backups_restore_a_real_world_without_losing_damaged_file(self):
        with tempfile.TemporaryDirectory(prefix='zodee-recovery-') as directory:
            result=subprocess.run([DRIVER,'recovery',MAP],cwd=WORKSPACE,env=self.environment(directory),text=True,capture_output=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('recovery-ok',result.stdout)

    def test_display_matrix_resize_text_scale_fullscreen_and_input(self):
        with tempfile.TemporaryDirectory(prefix='zodee-display-') as directory:
            result=subprocess.check_output([DRIVER,'display-matrix',directory],cwd=WORKSPACE,env=self.environment(directory),text=True,timeout=60)
            self.assertIn('display-matrix-ok',result)
            self.assertEqual(len(list(pathlib.Path(directory).glob('display-*.png'))),32)

    def test_diagnostics_excludes_raw_errors_secrets_and_personal_paths(self):
        with tempfile.TemporaryDirectory(prefix='zodee-private-user-') as directory:
            env=self.environment(directory);env['DB_PASSWORD']='sensitive-test-value'
            report=subprocess.check_output([DRIVER,'content-health','/private/user/secrets?token=secret'],cwd=WORKSPACE,env=env,text=True).strip()
            text=pathlib.Path(report).read_text()
            self.assertIn('Import status: unknown',text)
            self.assertIn('Original levels:',text)
            self.assertIn('deployment_failed',text)
            for private in [directory,'sensitive-test-value','token=secret','/private/user']:
                self.assertNotIn(private,text)

    def test_fresh_profile_imported_authored_level_can_start_save_and_reload(self):
        # No source-tree game maps, artwork, or user's profile are available.
        with tempfile.TemporaryDirectory(prefix='zodee-fresh-') as directory:
            root=pathlib.Path(directory);install=root/'installation';install.mkdir();profile=root/'profile'
            env=self.environment(directory);env['ZOD_PROFILE_DIR']=str(profile)
            self.assertEqual(subprocess.check_output([DRIVER,'asset-ready'],cwd=install,env=env,text=True).strip(),'missing')
            subprocess.run([DRIVER,'asset-bootstrap'],cwd=install,env=env,check=True,capture_output=True,timeout=15)
            source=root/'owned-levels';source.mkdir();import source_fixtures;source_fixtures.write_map(source/'p02_bb_orig01.map')
            subprocess.run([GAME,'--import-levels',str(source)],cwd=install,env=env,check=True,capture_output=True,timeout=15)
            imported=profile/'content/maps/p02_bb_orig01.map';self.assertEqual(imported.read_bytes(),(source/'p02_bb_orig01.map').read_bytes())
            result=subprocess.run([DRIVER,'save','maps/p02_bb_orig01.map'],cwd=install,env=env,text=True,capture_output=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertFalse((install/'maps').exists())
            # Repeat import preserves the previously imported level.
            before=imported.read_bytes();subprocess.run([GAME,'--import-levels',str(source)],cwd=install,env=env,check=True,capture_output=True,timeout=15)
            self.assertEqual(imported.read_bytes(),before)

    def test_recovery_accessibility_save_previews_and_settings_back(self):
        with tempfile.TemporaryDirectory(prefix='zod-enhanced-polish-') as directory:
            result=subprocess.check_output([DRIVER,'polish',directory],cwd=WORKSPACE,env=self.environment(directory),text=True,timeout=20)
            self.assertIn('polish-ok',result)
            for name in ['diagnostics-review','build-info','multiplayer-guide','moderation-feedback','save-browser','editor-help','accessibility','campaign-polish','settings-2','settings-3','settings-4','editor-bindings','object-palette']:
                self.assertEqual((pathlib.Path(directory)/(name+'.png')).read_bytes()[:8],b'\x89PNG\r\n\x1a\n')

    def test_scaled_battle_menu_resume(self):
        with tempfile.TemporaryDirectory(prefix='zod-battle-menu-') as directory:
            image=pathlib.Path(directory)/'battle-menu.png'
            result=subprocess.check_output([DRIVER,'battle-menu',str(image)],cwd=WORKSPACE,env=self.environment(directory),text=True)
            self.assertIn('battle-menu-resume-ok',result)
            self.assertEqual(image.read_bytes()[:8],b'\x89PNG\r\n\x1a\n')

    def test_team_palette_accepts_legacy_trailing_row_without_recolouring_black(self):
        result = subprocess.check_output([DRIVER, 'palette-compatibility'], cwd=WORKSPACE, text=True)
        self.assertIn('palette-compatible', result)

    def environment(self, directory):
        env = os.environ.copy()
        for key in list(env):
            if key.startswith('ZOD_'):
                del env[key]
        env.update(HOME=directory, USERPROFILE=directory, TMPDIR=directory,
                   TEMP=directory, TMP=directory, SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy',ZOD_PROFILE_DIR=directory,ZOD_CAMPAIGN_DIR=str(pathlib.Path(directory)/'campaigns'))
        return env

    def test_match_results_idle_units_and_production_feedback(self):
        with tempfile.TemporaryDirectory(prefix='zod match ui ') as directory:
            env = self.environment(directory)
            env['ZOD_ORCHESTRATOR'] = 'local'
            image = pathlib.Path(directory) / 'results.png'
            result = subprocess.run([DRIVER, 'match-ui', str(image)], cwd=WORKSPACE, env=env,
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertGreater(image.stat().st_size, 1000)

    def test_lobby_roster_ready_team_changes_and_countdown(self):
        with tempfile.TemporaryDirectory(prefix='zod lobby ') as directory:
            env=self.environment(directory)
            image=pathlib.Path(directory)/'lobby.png'
            result=subprocess.run([DRIVER,'lobby',directory,str(image)],cwd=WORKSPACE,env=env,
                                  capture_output=True,text=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertGreater(image.stat().st_size,1000)
            with self.match(map_file=str(pathlib.Path(directory)/'lobby.map')) as (_, port, sockets):
                first=Client(sockets[0]); self.identify(first,'Alice',PROTOCOL['RED_TEAM'])
                sockets.append(socket.create_connection(('127.0.0.1',port),timeout=2))
                second=Client(sockets[-1]); self.identify(second,'Bob',PROTOCOL['BLUE_TEAM'])
                first.send('SET_READY',struct.pack('=i',1))
                self.assertEqual(struct.unpack('=ii',first.until('SET_LPLAYER_READY')[0])[1],1)
                first.send('SET_TEAM',struct.pack('=i',3)) # Third fort, still empty.
                first.until('SET_TEAM')
                first.send('REQUEST_PLAYER_LIST'); first.send('REQUEST_PLAYER_ID')
                _,packets=first.until('GIVE_PLAYER_ID')
                ready=[struct.unpack('=ii',body) for kind,body in packets if kind==PROTOCOL['SET_LPLAYER_READY']]
                self.assertTrue(ready); self.assertTrue(all(not value for _,value in ready))
                first.send('SET_READY',struct.pack('=i',1)); second.send('SET_READY',struct.pack('=i',1))
                # Ignore an initial waiting-state update if it preceded readiness.
                while True:
                    remaining=struct.unpack('=i',first.until('LOBBY_COUNTDOWN')[0])[0]
                    if remaining>0: break
                self.assertLessEqual(remaining,30)
                first.send('SET_READY',struct.pack('=i',0))
                while struct.unpack('=i',first.until('LOBBY_COUNTDOWN')[0])[0]!=-1: pass
                first.send('GET_GAME_PAUSED'); self.assertEqual(first.until('UPDATE_GAME_PAUSED')[0],b'\x01')

    def test_persistent_server_starts_a_fresh_lobby_after_results(self):
        with tempfile.TemporaryDirectory(prefix='zod persistent ') as directory:
            env=self.environment(directory)
            env['ZOD_PERSISTENT_SERVER']='1'
            result=subprocess.run([DRIVER,'persistent-round'],cwd=WORKSPACE,env=env,
                                  capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_real_world_save_load_roundtrip(self):
        with tempfile.TemporaryDirectory(prefix='zod-save-test-') as directory:
            result = subprocess.run([DRIVER, 'save', MAP], cwd=WORKSPACE,
                                    env=self.environment(directory), capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_random_map_repeatability_seed_bounds_and_corrupt_preferences(self):
        with tempfile.TemporaryDirectory(prefix='zod generator ') as directory:
            result = subprocess.run([DRIVER, 'random-module', directory], cwd=WORKSPACE, env=self.environment(directory),
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_random_setup_remembers_choices_across_processes_and_hosts_previewed_map(self):
        with tempfile.TemporaryDirectory(prefix='zod remembered setup ') as directory:
            env = self.environment(directory)
            env['ZOD_ORCHESTRATOR'] = 'local'
            image = pathlib.Path(directory) / 'random.png'
            result = subprocess.run([DRIVER, 'random-prepare', str(image)], cwd=WORKSPACE, env=env,
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertGreater(image.stat().st_size, 1000)
            env['ZOD_ORCHESTRATOR'] = '127.0.0.1:1' #generated maps must still host locally
            result = subprocess.run([DRIVER, 'random-restore', str(image)], cwd=WORKSPACE, env=env,
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_skirmish_setup_controls_preview_and_startup(self):
        with tempfile.TemporaryDirectory(prefix='zod skirmish ') as directory:
            env = self.environment(directory)
            env['ZOD_ORCHESTRATOR'] = 'local'
            settings = pathlib.Path(directory) / 'settings.txt'
            env['ZOD_TEST_SETTINGS_FILE'] = str(settings)
            image = pathlib.Path(directory) / 'preview.png'
            result = subprocess.run([DRIVER, 'skirmish', str(image)], cwd=WORKSPACE, env=env,
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertGreater(image.stat().st_size, 1000)
            difficulty, speed = settings.read_text().split()
            self.assertEqual(int(difficulty), 3)
            self.assertAlmostEqual(float(speed), 1.5)

    def test_local_fallback_creates_real_match_and_cleans_up(self):
        with tempfile.TemporaryDirectory(prefix='zod local match ') as directory:
            env = self.environment(directory)
            env['ZOD_ORCHESTRATOR'] = 'local'
            result = subprocess.run([DRIVER, 'local-host', pathlib.Path(MAP).name], cwd=WORKSPACE,
                                    env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            port = int(re.search(r'LOCAL_MATCH_PORT=(\d+)', result.stdout)[1])
            with socket.socket() as probe:
                self.assertNotEqual(probe.connect_ex(('127.0.0.1', port)), 0, 'server survived parent exit')

    def test_imported_levels_resolve_from_the_user_profile(self):
        with tempfile.TemporaryDirectory(prefix='zodee-level-import-') as directory:
            before=pathlib.Path(MAP).read_bytes()
            result=subprocess.check_output([DRIVER,'import-levels',str(pathlib.Path(MAP).parent)],cwd=WORKSPACE,env=self.environment(directory),text=True)
            self.assertIn('levels-imported',result)
            self.assertEqual((pathlib.Path(directory)/'content/maps/p02_bb_orig01.map').read_bytes(),before)
            self.assertEqual(pathlib.Path(MAP).read_bytes(),before)

    def test_import_helper_in_macos_bundle_and_profile_overlay(self):
        with tempfile.TemporaryDirectory(prefix='zod-import-layout-') as directory:
            root=pathlib.Path(directory);resources=root/'Contents/Resources';binary=root/'Contents/MacOS'
            resources.mkdir(parents=True);binary.mkdir()
            helper=binary/('zod-asset-importer.exe' if os.name=='nt' else 'zod-asset-importer');helper.write_bytes(b'test')
            env=self.environment(directory)
            found=subprocess.check_output([DRIVER,'asset-helper',str(resources)+'/'],env=env,text=True).strip()
            self.assertEqual(pathlib.Path(found).resolve(),helper.resolve())
            profile=root/'profile';assets=profile/'imports/disc-test/assets';(assets/'original-z').mkdir(parents=True)
            (assets/'original-z/manifest.json').write_text('{}');(assets/'sounds').mkdir();media=assets/'sounds/test.wav';media.write_bytes(b'owned sample')
            (profile/'import-active.txt').write_text('disc-test\n');env['ZOD_PROFILE_DIR']=str(profile)
            found=subprocess.check_output([DRIVER,'asset-path','assets/sounds/test.wav'],env=env,text=True).strip()
            self.assertEqual(pathlib.Path(found).resolve(),media.resolve())
            missing=subprocess.check_output([DRIVER,'asset-path','assets/sounds/missing.wav'],env=env,text=True).strip()
            self.assertEqual(missing,'')
            unsafe=subprocess.check_output([DRIVER,'asset-path','assets/../test.wav'],env=env,text=True).strip()
            self.assertEqual(unsafe,'')
            # Earlier imports lack the newly recovered graphics: require an update
            # rather than entering gameplay with missing UI and animation frames.
            for name in ('planets/desert.bmp','units/robots/stand_red_r000.png','sounds/AD1.MID'):
                path=assets/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture')
            self.assertEqual(subprocess.check_output([DRIVER,'asset-ready'],cwd=WORKSPACE,env=env,text=True),'missing')
            recovered=assets/'buildings/level_1.bmp';recovered.parent.mkdir(parents=True,exist_ok=True);recovered.write_bytes(b'fixture')
            self.assertEqual(subprocess.check_output([DRIVER,'asset-ready'],cwd=WORKSPACE,env=env,text=True),'missing')
            controls=assets/'other/menus/create_button.png';controls.parent.mkdir(parents=True,exist_ok=True);controls.write_bytes(b'fixture')
            overlay=assets/'other/production_gui/base_image.png';overlay.parent.mkdir(parents=True,exist_ok=True);overlay.write_bytes(b'fixture')
            bar=assets/'other/main_menu_gui/list/list_button_down_normal.png';bar.parent.mkdir(parents=True,exist_ok=True);bar.write_bytes(b'fixture')
            panel=assets/'other/map_items/map_object0.png';panel.parent.mkdir(parents=True,exist_ok=True);panel.write_bytes(b'fixture')
            self.assertEqual(subprocess.check_output([DRIVER,'asset-ready'],cwd=WORKSPACE,env=env,text=True),'missing')
            for name in ('teams/blue_palette.bmp','summary_won.png','frontend/disc/main_menu.png','sounds/MACHGUN2.wav','sounds/RICOCH1.wav'):
                path=assets/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture')
            self.assertEqual(subprocess.check_output([DRIVER,'asset-ready'],cwd=WORKSPACE,env=env,text=True),'ready')

    def test_clean_checkout_can_render_import_screen_without_game_media(self):
        with tempfile.TemporaryDirectory(prefix='zod-clean-profile-') as directory:
            env=self.environment(directory);env['ZOD_PROFILE_DIR']=directory
            # The source tree deliberately no longer contains this original sprite.
            self.assertFalse((pathlib.Path(ROOT)/'assets/units/robots/stand_red_r000.png').exists())
            missing=subprocess.check_output([DRIVER,'asset-ready'],cwd=WORKSPACE,env=env,text=True)
            self.assertEqual(missing,'missing')
            result=subprocess.run([DRIVER,'asset-bootstrap'],cwd=WORKSPACE,env=env,capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('import-screen-rendered',result.stdout)

    def test_game_routes_clean_profile_to_import_before_server_startup(self):
        with tempfile.TemporaryDirectory(prefix='zod-first-launch-') as directory:
            env=self.environment(directory);env['ZOD_PROFILE_DIR']=directory
            with socket.socket() as reserve:
                reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
            env['ZOD_PORT']=str(port)
            log_path=pathlib.Path(directory)/'startup.log'
            with log_path.open('w+') as log:
                process=subprocess.Popen([GAME,'-w','-s','-u'],cwd=WORKSPACE,env=env,stdout=log,stderr=subprocess.STDOUT)
                try:
                    deadline=time.monotonic()+5
                    while 'opening offline disc importer' not in log_path.read_text():
                        self.assertIsNone(process.poll(),log_path.read_text())
                        if time.monotonic()>deadline:self.fail(log_path.read_text())
                        time.sleep(.02)
                    with socket.socket() as probe:
                        self.assertNotEqual(probe.connect_ex(('127.0.0.1',port)),0)
                finally:
                    process.terminate();process.wait(timeout=5)

    def test_campaign_can_disable_inherited_cloud_requirement(self):
        with tempfile.TemporaryDirectory(prefix='zod-offline-') as directory:
            env=self.environment(directory)
            env['ZOD_REQUIRE_CLOUD']='1'
            result=subprocess.run([DRIVER,'offline-policy'],cwd=WORKSPACE,env=env,capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_embedded_server_transfers_maps_on_small_worker_stack(self):
        with self.match(embedded=True) as (process,port,sockets):
            client=Client(sockets[0]);self.identify(client,'Embedded stack test',PROTOCOL['RED_TEAM'])
            for _ in range(3):
                self.download_world(client)
                client.send('REQUEST_VERSION');self.assertTrue(client.until('GIVE_VERSION')[0])
                self.assertIsNone(process.poll())

    def test_malformed_and_fragmented_packet_decoder(self):
        result = subprocess.run([DRIVER, 'packets'], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @contextlib.contextmanager
    def match(self, private=False, bots=(), map_file=MAP, embedded=False):
        with tempfile.TemporaryDirectory(prefix='zod-match-test-') as directory:
            with socket.socket() as reserve:
                reserve.bind(('127.0.0.1', 0))
                port = reserve.getsockname()[1]
            env = self.environment(directory)
            env.update(ZOD_PORT=str(port), ZOD_EMPTY_EXIT_SECS='0')
            log_path = pathlib.Path(directory) / 'server.log'
            settings = pathlib.Path(directory) / 'perpetual.txt'
            settings.write_text('start_map_paused=1\n')
            with log_path.open('w+') as log:
                command = [DRIVER, 'embedded-stack-server', '-m', map_file] if embedded else ([DRIVER, '-private', '-m', map_file] if private else [GAME, '-d', '-m', map_file, '-e', str(settings)])
                for bot in bots:
                    command.extend(['-b', bot])
                process = subprocess.Popen(command,
                                           cwd=WORKSPACE, env=env, stdout=log, stderr=subprocess.STDOUT)
                clients = []
                try:
                    deadline = time.monotonic() + 10
                    while True:
                        self.assertIsNone(process.poll(), log_path.read_text())
                        try:
                            first = socket.create_connection(('127.0.0.1', port), timeout=.2)
                            clients.append(first)
                            break
                        except OSError:
                            if time.monotonic() >= deadline:
                                self.fail('server did not start: ' + log_path.read_text())
                            time.sleep(.02)
                    yield process, port, clients
                    self.assertIsNone(process.poll(), log_path.read_text())
                except Exception as error:
                    error.add_note(log_path.read_text()) if hasattr(error, 'add_note') else None
                    raise
                finally:
                    for client in clients:
                        client.close()
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3)

    def discover(self, *ports):
        result = subprocess.run([DRIVER, 'discover', *map(str, ports)], capture_output=True, text=True, check=True, timeout=5)
        return json.loads(result.stdout)

    def test_direct_address_validation(self):
        valid = {'192.168.1.20': ('192.168.1.20', 2137),
                 ' 127.0.0.1:2301 ': ('127.0.0.1', 2301), 'localhost:65535': ('localhost', 65535),
                 'game-host.local:1': ('game-host.local', 1)}
        for address, expected in valid.items():
            result = subprocess.run([DRIVER, 'address', address], capture_output=True, text=True, timeout=3)
            self.assertEqual(result.returncode, 0, result.stderr)
            host, port = result.stdout.splitlines()
            self.assertEqual((host, int(port)), expected)
        for address in ['', ':2137', '1.2.3.999', '127.0.0.1:', 'localhost:0', 'localhost:65536',
                        'localhost:-1', 'localhost:12x', '[::1]:2137', 'http://host', 'a..b', '-host', 'bad host', 'x\nname']:
            result = subprocess.run([DRIVER, 'address', address], capture_output=True, text=True, timeout=3)
            self.assertNotEqual(result.returncode, 0, address)

    def test_browser_address_entry_and_full_match_states(self):
        with tempfile.TemporaryDirectory() as directory:
            image=pathlib.Path(directory)/'browser.png'
            result = subprocess.run([DRIVER, 'browser', str(image)], cwd=WORKSPACE, env=self.environment(directory), capture_output=True, text=True, timeout=8)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertGreater(image.stat().st_size,1000)

    def test_lan_discovers_multiple_hosts_and_current_status(self):
        with self.match() as (_, first_port, sockets):
            with self.match() as (_, second_port, _):
                games = self.discover(first_port, second_port)
                self.assertEqual({m['port'] for m in games}, {first_port, second_port})
                self.assertTrue(all(m['capacity'] == 2 and not m['started'] for m in games))
                client = Client(sockets[0]); self.identify(client, 'LAN Player', PROTOCOL['RED_TEAM'])
                games = self.discover(first_port)
                self.assertEqual(games[0]['players'], 1)
                client.send('SET_GAME_PAUSED', b'\x00')
                client.send('GET_GAME_PAUSED')
                self.assertEqual(client.until('UPDATE_GAME_PAUSED')[0], b'\x00')
                self.assertTrue(self.discover(first_port)[0]['started'])
                client.send('SET_GAME_PAUSED', b'\x01')
                for _ in range(5):
                    if client.until('UPDATE_GAME_PAUSED')[0] == b'\x01':
                        break
                else:
                    self.fail('server did not pause')
                self.assertTrue(self.discover(first_port)[0]['started'], 'pausing a running match made it look joinable')

    def test_lan_capacity_excludes_bot_occupied_forts(self):
        with self.match(bots=('blue',)) as (_, port, sockets):
            games = self.discover(port)
            self.assertEqual(len(games), 1)
            self.assertEqual(games[0]['capacity'], 1)
            client = Client(sockets[0])
            deadline = time.monotonic() + 3
            bot_ready = False
            while time.monotonic() < deadline:
                client.send('REQUEST_PLAYER_LIST'); client.send('REQUEST_PLAYER_ID')
                _, packets = client.until('GIVE_PLAYER_ID')
                teams = {struct.unpack('=ii', body)[0]: struct.unpack('=ii', body)[1]
                         for kind, body in packets if kind == PROTOCOL['SET_LPLAYER_TEAM']}
                modes = {struct.unpack('=ii', body)[0]: struct.unpack('=ii', body)[1]
                         for kind, body in packets if kind == PROTOCOL['SET_LPLAYER_MODE']}
                bot_ready = any(mode == PROTOCOL['BOT_MODE'] and teams.get(pid) == PROTOCOL['BLUE_TEAM'] for pid, mode in modes.items())
                if bot_ready:
                    break
                time.sleep(.02)
            self.assertTrue(bot_ready)
            self.assertEqual(self.identify(client, 'Human Host', PROTOCOL['RED_TEAM']), PROTOCOL['RED_TEAM'])

    def test_single_player_is_not_advertised(self):
        with self.match(private=True) as (_, port, _):
            self.assertEqual(self.discover(port), [])

    def test_direct_connection_adopts_real_socket_and_reports_refusal(self):
        with self.match() as (_, port, _):
            for host in ['127.0.0.1', 'localhost']:
                result = subprocess.run([DRIVER, 'dial', host, str(port)], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        start = time.monotonic()
        result = subprocess.run([DRIVER, 'dial', '127.0.0.1', str(port)], capture_output=True, text=True, timeout=4)
        self.assertNotEqual(result.returncode, 0)
        self.assertLess(time.monotonic() - start, 3)
        self.assertIn('refused', result.stderr.lower())

    def test_version_preflight_rejects_incompatible_or_silent_servers(self):
        for payload,expected in [(b'9.9-incompatible\0'.ljust(50,b'\0'),'Version mismatch'),(b'broken','invalid version'),(None,'timed out')]:
            with socket.socket() as listener:
                listener.bind(('127.0.0.1',0));listener.listen();port=listener.getsockname()[1]
                def respond():
                    connection,_=listener.accept()
                    with connection:
                        client=Client(connection);kind,_=client.packet()
                        self.assertEqual(kind,PROTOCOL['REQUEST_VERSION'])
                        if payload is not None:
                            connection.sendall(struct.pack('=ii',len(payload),PROTOCOL['GIVE_VERSION'])+payload)
                        else:time.sleep(1.8)
                thread=threading.Thread(target=respond,daemon=True);thread.start()
                result=subprocess.run([DRIVER,'dial','127.0.0.1',str(port)],capture_output=True,text=True,timeout=5)
                thread.join(3);self.assertFalse(thread.is_alive());self.assertNotEqual(result.returncode,0)
                self.assertIn(expected,result.stderr)

    def test_private_multiplayer_reconnects_and_cancel_stops_retries(self):
        with self.match() as (_,port,_):
            with tempfile.TemporaryDirectory() as directory:
                result=subprocess.run([DRIVER,'multiplayer-reconnect',str(port)],cwd=WORKSPACE,env=self.environment(directory),capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_discovery_rejects_malformed_replayed_and_duplicate_replies(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(('127.0.0.1', 0)); server.settimeout(3)
            port = server.getsockname()[1]
            errors = []
            def respond():
                try:
                    query, peer = server.recvfrom(512)
                    name = b'test.map'
                    def packet(token=query[8:16], instance=1, length=len(name), text=name, advertised=port):
                        return b'ZODLAN1!' + token + struct.pack('>QHBBBB', instance, advertised, 0, 2, 0, length) + text
                    for reply in [b'junk', packet(token=b'0' * 8), packet(length=96),
                                  packet(advertised=1), packet(text=b'bad\nname'), packet(), packet()]:
                        server.sendto(reply, peer)
                except Exception as error:
                    errors.append(error)
            worker = threading.Thread(target=respond); worker.start()
            games = self.discover(port); worker.join(4)
            self.assertFalse(worker.is_alive()); self.assertFalse(errors, errors)
            self.assertEqual(len(games), 1)
            self.assertEqual(games[0]['port'], port)

    def identify(self, client, name, team):
        client.send('SET_NAME', name.encode() + b'\0')
        client.send('SET_PLAYER_MODE', bytes([PROTOCOL['PLAYER_MODE']]))
        client.send('SET_TEAM', struct.pack('=i', team))
        body, _ = client.until('SET_TEAM')
        return struct.unpack('=i', body)[0]

    def download_world(self, client):
        client.send('REQUEST_MAP')
        downloaded = bytearray()
        expected_chunk = 0
        while True:
            payload, _ = client.until('STORE_MAP')
            chunk = struct.unpack('=i', payload[:4])[0]
            if chunk == -1:
                break
            self.assertEqual(chunk, expected_chunk)
            expected_chunk += 1
            downloaded.extend(payload[4:])
        self.assertEqual(bytes(downloaded), pathlib.Path(MAP).read_bytes())
        # An ordered request/response marks the end of each unmarked list.
        for request, response in [('REQUEST_OBJECTS', 'ADD_NEW_OBJECT'), ('REQUEST_ZONES', 'SET_ZONE_INFO')]:
            client.send(request)
            client.send('REQUEST_PLAYER_ID')
            _, packets = client.until('GIVE_PLAYER_ID')
            entries = [body for kind, body in packets if kind == PROTOCOL[response]]
            self.assertTrue(entries, f'{request} produced no entries')
            if request == 'REQUEST_OBJECTS':
                self.assertTrue(all(len(body) == PROTOCOL['object_init_size'] for body in entries))
                refs = [struct.unpack_from('=i', body, 8)[0] for body in entries]
                self.assertEqual(len(refs), len(set(refs)))
        client.send('REQUEST_SETTINGS')
        self.assertTrue(client.until('SET_SETTINGS')[0])

    def test_complete_two_player_match_startup(self):
        with self.match() as (process, port, sockets):
            first = Client(sockets[0])
            self.assertEqual(self.identify(first, 'Regression One', PROTOCOL['RED_TEAM']), PROTOCOL['RED_TEAM'])
            first.send('GET_GAME_PAUSED')
            self.assertEqual(first.until('UPDATE_GAME_PAUSED')[0], b'\x01')
            self.download_world(first)
            sockets.append(socket.create_connection(('127.0.0.1', port), timeout=2))
            second = Client(sockets[-1])
            # Request the occupied team: the server must assign the other fort.
            self.assertEqual(self.identify(second, 'Regression Two', PROTOCOL['RED_TEAM']), PROTOCOL['BLUE_TEAM'])
            self.download_world(second)
            sockets.append(socket.create_connection(('127.0.0.1',port),timeout=2))
            spectator=Client(sockets[-1])
            self.assertEqual(self.identify(spectator,'Observer',PROTOCOL['RED_TEAM']),0)
            spectator.send('SET_READY',struct.pack('=i',1))
            self.assertEqual(struct.unpack('=ii',spectator.until('SET_LPLAYER_READY')[0])[1],0)
            first.send('SET_READY', struct.pack('=i', 1))
            second.send('SET_READY', struct.pack('=i', 1))
            self.assertEqual(first.until('UPDATE_GAME_PAUSED')[0], b'\x00')
            self.assertEqual(second.until('UPDATE_GAME_PAUSED')[0], b'\x00')
            first.send('GET_GAME_PAUSED')
            self.assertEqual(first.until('UPDATE_GAME_PAUSED')[0], b'\x00')
            # The original/campaign results action uses this authorized reset
            # path, retaining the map and all connected players.
            first.send('RESET_MAP')
            first.until('RESET_GAME')
            second.until('RESET_GAME')
            self.download_world(first)
            self.download_world(second)

    def test_private_rejoin_reserves_team_and_consumes_session_token(self):
        with self.match() as (_,port,sockets):
            first=Client(sockets[0]);token=first.until('RESUME_SESSION')[0]
            self.assertEqual(len(token),65)
            self.assertEqual(self.identify(first,'Reserved player',PROTOCOL['RED_TEAM']),PROTOCOL['RED_TEAM'])
            first.connection.close();time.sleep(.15)
            sockets.append(socket.create_connection(('127.0.0.1',port),timeout=2));other=Client(sockets[-1])
            self.assertEqual(self.identify(other,'Different player',PROTOCOL['RED_TEAM']),PROTOCOL['BLUE_TEAM'])
            sockets.append(socket.create_connection(('127.0.0.1',port),timeout=2));resumed=Client(sockets[-1]);resumed.until('RESUME_SESSION');resumed.send('RESUME_SESSION',token)
            self.assertEqual(struct.unpack('=i',resumed.until('SET_TEAM')[0])[0],PROTOCOL['RED_TEAM'])
            self.assertEqual(self.identify(resumed,'Reserved player',PROTOCOL['RED_TEAM']),PROTOCOL['RED_TEAM'])
            sockets.append(socket.create_connection(('127.0.0.1',port),timeout=2));replay=Client(sockets[-1]);replay.send('RESUME_SESSION',token)
            self.assertEqual(self.identify(replay,'Token replay',PROTOCOL['RED_TEAM']),0)

    def test_explicit_spectator_choice_releases_team_and_clears_ready(self):
        with self.match() as (_,port,sockets):
            first=Client(sockets[0]);self.assertEqual(self.identify(first,'Spectating',PROTOCOL['RED_TEAM']),PROTOCOL['RED_TEAM'])
            first.send('SET_READY',struct.pack('=i',1));first.until('SET_LPLAYER_READY')
            first.send('SET_TEAM',struct.pack('=i',-1));self.assertEqual(struct.unpack('=i',first.until('SET_TEAM')[0])[0],0)
            first.send('REQUEST_PLAYER_LIST');first.send('REQUEST_PLAYER_ID');_,packets=first.until('GIVE_PLAYER_ID')
            self.assertTrue(all(struct.unpack('=ii',body)[1]==0 for kind,body in packets if kind==PROTOCOL['SET_LPLAYER_READY']))
            sockets.append(socket.create_connection(('127.0.0.1',port),timeout=2));second=Client(sockets[-1])
            self.assertEqual(self.identify(second,'New player',PROTOCOL['RED_TEAM']),PROTOCOL['RED_TEAM'])

    def test_server_survives_malformed_peer(self):
        with self.match() as (process, port, sockets):
            healthy = Client(sockets[0])
            self.identify(healthy, 'Healthy Peer', PROTOCOL['RED_TEAM'])
            # Invalid message IDs and undersized/unterminated payloads must be
            # ignored without corrupting a healthy client's connection.
            healthy.connection.sendall(struct.pack('=ii', 0, -1) + struct.pack('=ii', 0, 2147483647))
            healthy.send('SET_TEAM', b'\x01')
            healthy.send('SET_TEAM', struct.pack('=i', -1))
            healthy.send('SET_NAME', b'unterminated')
            healthy.send('REQUEST_PLAYER_ID')
            self.assertEqual(len(healthy.until('GIVE_PLAYER_ID')[0]), 4)
            for size in (-1, 2147483647, 400000):
                with socket.create_connection(('127.0.0.1', port), timeout=2) as attacker:
                    attacker.sendall(struct.pack('=ii', size, PROTOCOL['SET_NAME']))
                    attacker.settimeout(3)
                    # Drain connect announcements until the malformed peer is closed.
                    try:
                        while attacker.recv(4096):
                            pass
                    except ConnectionResetError:
                        pass
                healthy.send('REQUEST_PLAYER_ID')
                self.assertEqual(len(healthy.until('GIVE_PLAYER_ID')[0]), 4)
                self.assertIsNone(process.poll())

unittest.main()
