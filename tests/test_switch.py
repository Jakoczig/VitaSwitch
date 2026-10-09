"""Host-side fault-injection tests for the VitaSwitch file transaction.
Run with: python3 tests/test_switch.py
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'tests' / 'vitaswitch_host'


def setup_vita(root):
    for name in ['ur0/tai', 'ur0/data/PSVshell', 'ux0/data/VitaGrafix',
                 'ur0/appmeta/SWCH00001/livearea/contents']:
        (root / name).mkdir(parents=True, exist_ok=True)
    (root / 'ur0/tai/config.txt').write_text('initial config\n')
    (root / 'ur0/data/PSVshell/profiles').mkdir()
    (root / 'ur0/data/PSVshell/profiles/default').mkdir()
    (root / 'ur0/data/PSVshell/profiles/default/clock.txt').write_text('100mhz\n')
    (root / 'ux0/data/VitaGrafix/config.txt').write_text('initial vg\n')
    assets = root / 'ur0/appmeta/SWCH00001/livearea/contents'
    for name in ['bg.png', 'bg_portable.png', 'bg_docked.png', 'icon_portable.png', 'icon_docked.png']:
        (assets/name).write_text(name)
    (root / 'ur0/appmeta/SWCH00001/icon0.png').write_text('icon0.png')


class TestVitaSwitch(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        setup_vita(self.root)
        self.tai = self.root / 'ur0/tai'
        self.psvs = self.root / 'ur0/data/PSVshell'
        self.vg = self.root / 'ux0/data/VitaGrafix'

    def app(self, extra=None, expect=0):
        env = os.environ.copy()
        env.update({'VITA_MOCK_ROOT': str(self.root)})
        for key in ['VITA_INJECT_OP', 'VITA_INJECT_AT', 'VITA_INJECT_CRASH',
                    'VITA_INJECT_PATH', 'VITA_SHORT_WRITES']:
            env.pop(key, None)
        if extra:
            env.update(extra)
        p = subprocess.run([str(BINARY)], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if p.returncode != expect:
            raise AssertionError(f'expected exit {expect}, got {p.returncode}, stderr={p.stderr.decode(errors="replace")}')
        return p

    def initialize(self):
        self.app()
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '0')
        self.assertFalse((self.root/'rebooted').exists())

    def assert_no_transaction(self):
        self.assertFalse((self.tai/'vitaswitch.transaction').exists())
        self.assertFalse((self.tai/'vitaswitch.committed').exists())
        self.assertFalse((self.tai/'vitaswitch.committed.tmp').exists())

    def test_setup_does_not_overwrite_preexisting_configuration(self):
        (self.tai/'config_portable.txt').write_text('old custom portable\n')
        self.initialize()
        self.assertEqual((self.tai/'config_portable.txt').read_text(), 'old custom portable\n')
        self.assertEqual((self.tai/'config_docked.txt').read_text(), 'initial config\n')

    def test_switch_roundtrip_and_edits_are_preserved(self):
        self.initialize()
        (self.tai/'config_docked.txt').write_text('docked baseline\n')
        (self.vg/'config_docked.txt').write_text('docked graphics\n')
        (self.psvs/'profiles_docked/default/clock.txt').write_text('500mhz\n')
        (self.tai/'config.txt').write_text('portable custom\n')
        self.app()
        self.assertTrue((self.root/'rebooted').exists())
        self.assertEqual((self.tai/'config.txt').read_text(), 'docked baseline\n')
        self.assertEqual((self.tai/'config_portable.txt').read_text(), 'portable custom\n')
        self.assertEqual((self.psvs/'profiles/default/clock.txt').read_text(), '500mhz\n')
        self.assertEqual((self.vg/'config.txt').read_text(), 'docked graphics\n')
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        assets = self.root / 'ur0/appmeta/SWCH00001'
        self.assertEqual((assets/'livearea/contents/bg.png').read_text(), 'bg_docked.png')
        self.assertEqual((assets/'icon0.png').read_text(), 'icon_docked.png')
        self.assert_no_transaction()
        (self.tai/'config.txt').write_text('docked custom\n')
        self.app()
        self.assertEqual((self.tai/'config.txt').read_text(), 'portable custom\n')
        self.assertEqual((self.tai/'config_docked.txt').read_text(), 'docked custom\n')
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '0')
        self.assert_no_transaction()

    def test_missing_next_config_refuses_without_reboot(self):
        self.initialize()
        (self.tai/'config_docked.txt').unlink()
        old = (self.tai/'config.txt').read_text()
        self.app(expect=1)
        self.assertEqual((self.tai/'config.txt').read_text(), old)
        self.assertFalse((self.root/'rebooted').exists())
        self.assert_no_transaction()

    def test_failed_io_before_commit_restores_config(self):
        for fail in [('read', '1'), ('write', '2'), ('sync', '2'), ('rename', '2'), ('rename', '4'), ('rename', '7')]:
            with self.subTest(fail=fail):
                with tempfile.TemporaryDirectory() as td:
                    self.root = Path(td)
                    setup_vita(self.root)
                    self.tai = self.root / 'ur0/tai'
                    self.psvs = self.root / 'ur0/data/PSVshell'
                    self.vg = self.root / 'ux0/data/VitaGrafix'
                    self.initialize()
                    (self.tai/'config_docked.txt').write_text('docked baseline\n')
                    old = (self.tai/'config.txt').read_text()
                    self.app({'VITA_INJECT_OP':fail[0], 'VITA_INJECT_AT':fail[1]}, expect=1)
                    self.assertEqual((self.tai/'config.txt').read_text(), old)
                    self.assertEqual((self.tai/'switchstate.txt').read_text(), '0')
                    self.assertFalse((self.root/'rebooted').exists())
                    self.assert_no_transaction()

    def test_crash_after_active_renamed_is_recovered(self):
        self.initialize()
        (self.tai/'config_docked.txt').write_text('docked\n')
        self.app({'VITA_INJECT_OP':'rename', 'VITA_INJECT_AT':'2', 'VITA_INJECT_CRASH':'1'}, expect=77)
        self.assertTrue((self.tai/'vitaswitch.transaction').exists())
        self.app()
        self.assertEqual((self.tai/'config.txt').read_text(), 'initial config\n')
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '0')
        self.assert_no_transaction()
        self.assertFalse((self.root/'rebooted').exists())
        self.app()
        self.assertEqual((self.tai/'config.txt').read_text(), 'docked\n')

    def test_crash_after_state_write_rolls_back(self):
        self.initialize()
        self.app({'VITA_INJECT_OP':'open', 'VITA_INJECT_PATH':'vitaswitch.committed.tmp',
                  'VITA_INJECT_CRASH':'1'}, expect=77)
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        self.app()
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '0')
        self.assert_no_transaction()
        self.assertFalse((self.root/'rebooted').exists())

    def test_crash_after_commit_does_not_revert(self):
        self.initialize()
        self.app({'VITA_INJECT_OP':'remove','VITA_INJECT_PATH':'.vsw-old',
                  'VITA_INJECT_CRASH':'1'}, expect=77)
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        self.assertTrue((self.tai/'vitaswitch.committed').exists())
        self.app()
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        self.assert_no_transaction()
        self.assertFalse((self.root/'rebooted').exists())

    def test_crash_in_artwork_does_not_double_toggle(self):
        self.initialize()
        self.app({'VITA_INJECT_OP':'rename', 'VITA_INJECT_PATH':'icon0.png',
                  'VITA_INJECT_CRASH':'1'}, expect=77)
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        self.assert_no_transaction()
        self.app()
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        self.assertFalse((self.root/'rebooted').exists())
        self.app()
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '0')

    def test_crash_between_journal_removal_and_commit_removal(self):
        self.initialize()
        self.app({'VITA_INJECT_OP':'remove', 'VITA_INJECT_PATH':'vitaswitch.committed',
                  'VITA_INJECT_CRASH':'1'}, expect=77)
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        self.assertFalse((self.tai/'vitaswitch.transaction').exists())
        self.assertTrue((self.tai/'vitaswitch.committed').exists())
        self.app()
        self.assert_no_transaction()
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        self.assertFalse((self.root/'rebooted').exists())

    def test_crash_at_each_commit_rename_preserves_configuration(self):
        # All 4 swaps for each of three resources, plus the commit rename.
        for step in range(1, 14):
            with self.subTest(rename=step), tempfile.TemporaryDirectory() as td:
                self.root = Path(td)
                setup_vita(self.root)
                self.tai = self.root / 'ur0/tai'
                self.psvs = self.root / 'ur0/data/PSVshell'
                self.vg = self.root / 'ux0/data/VitaGrafix'
                self.initialize()
                (self.tai/'config_docked.txt').write_text('docked distinct\n')
                self.app({'VITA_INJECT_OP':'rename','VITA_INJECT_AT':str(step),
                          'VITA_INJECT_CRASH':'1'},expect=77)
                self.app()  # recovery only, must not toggle
                self.assertEqual((self.tai/'switchstate.txt').read_text(), '0')
                self.assertEqual((self.tai/'config.txt').read_text(), 'initial config\n')
                self.assertEqual((self.tai/'config_docked.txt').read_text(), 'docked distinct\n')
                self.assert_no_transaction()
                self.assertFalse((self.root/'rebooted').exists())
                self.app()  # normal switch is still possible
                self.assertEqual((self.tai/'config.txt').read_text(), 'docked distinct\n')

    def test_orphan_scratch_refuses_to_overwrite(self):
        self.initialize()
        (self.tai/'config.txt.vsw-old').write_text('unknown data')
        self.app(expect=1)
        self.assertEqual((self.tai/'config.txt.vsw-old').read_text(), 'unknown data')
        self.assertFalse((self.root/'rebooted').exists())

    def test_profile_symlink_is_rejected_at_setup(self):
        (self.psvs/'profiles/untrusted').symlink_to(self.tai/'config.txt')
        self.app(expect=1)
        self.assertFalse((self.tai/'switchconf.txt').exists())
        self.assertFalse((self.root/'rebooted').exists())

    def test_later_plugin_install_is_included(self):
        (self.psvs/'profiles/default/clock.txt').unlink()
        (self.psvs/'profiles/default').rmdir()
        (self.psvs/'profiles').rmdir()
        (self.vg/'config.txt').unlink()
        self.initialize()
        (self.psvs/'profiles').mkdir()
        (self.psvs/'profiles/test').write_text('clock')
        (self.vg/'config.txt').write_text('enabled')
        self.app()
        self.assertEqual((self.psvs/'profiles_portable/test').read_text(), 'clock')
        self.assertEqual((self.psvs/'profiles/test').read_text(), 'clock')
        self.assertEqual((self.vg/'config_portable.txt').read_text(), 'enabled')
        self.assert_no_transaction()

    def test_short_writes_supported(self):
        self.app({'VITA_SHORT_WRITES':'1'})
        self.assertEqual((self.tai/'config_docked.txt').read_text(), 'initial config\n')
        self.app({'VITA_SHORT_WRITES':'1'})
        self.assert_no_transaction()

    def test_removed_setup_marker_preserves_existing_state(self):
        self.initialize()
        self.app()
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        (self.tai/'switchconf.txt').unlink()
        (self.root/'rebooted').unlink()
        self.app()
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')
        self.assertFalse((self.root/'rebooted').exists())

    def test_missing_optional_incoming_backup_with_existing_mode_aborts(self):
        self.initialize()
        (self.psvs/'profiles_docked').rename(self.psvs/'corrupt-copy')
        self.app(expect=1)
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '0')
        self.assertFalse((self.root/'rebooted').exists())

    def test_malformed_mode_aborts(self):
        self.initialize()
        (self.tai/'switchstate.txt').write_text('01')
        self.app(expect=1)
        self.assertFalse((self.root/'rebooted').exists())

    def test_missing_state_on_legacy_init(self):
        self.initialize()
        (self.tai/'switchstate.txt').unlink()
        self.app()
        self.assertEqual((self.tai/'switchstate.txt').read_text(), '1')


if __name__ == '__main__':
    if not BINARY.exists():
        subprocess.check_call(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                               '-I', str(ROOT/'tests/mock_include'), str(ROOT/'main.c'),
                               str(ROOT/'tests/mock_vita.c'), '-o', str(BINARY)])
    unittest.main(verbosity=2)
