"""check.sh configures a build that was never configured, once, before any build."""
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
# Records its call; `f` exits with $STUB_F, a build fails so no test binary is needed.
STUB = """#!/usr/bin/env bash
echo "$(basename "$0") $*" >> "$(dirname "$0")/calls"
[[ $1 == f ]] && exit "${STUB_F:-0}"
exit 1
"""


class Configure(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        (self.root / 'scripts').mkdir()
        shutil.copy(SCRIPTS / 'check.sh', self.root / 'scripts')
        for name in ('xmake-linux.sh', 'xmake-msvc.sh'):
            stub = self.root / 'scripts' / name
            stub.write_text(STUB)
            stub.chmod(stub.stat().st_mode | stat.S_IEXEC)

    def configure(self, config):
        conf = self.root / 'build' / config / '.xmake' / 'linux' / 'x86_64' / 'xmake.conf'
        conf.parent.mkdir(parents=True)
        conf.touch()

    def run_check(self, *arguments, stub_f=0):
        calls = self.root / 'scripts' / 'calls'
        calls.unlink(missing_ok=True)
        environment = {key: value for key, value in os.environ.items() if key != 'COOP_SKYRIM_VERSION'}
        environment['STUB_F'] = str(stub_f)
        self.output = subprocess.run([self.root / 'scripts' / 'check.sh', *arguments], env=environment,
                                     capture_output=True, text=True).stdout
        return calls.read_text().splitlines()

    def test_unconfigured_configures_both_before_building(self):
        calls = self.run_check()
        self.assertEqual(calls[:2], ['xmake-linux.sh f', 'xmake-msvc.sh f -m dev -y'])
        self.assertTrue(calls[2].startswith('xmake-linux.sh build'))
        self.assertEqual(sum(' f' in call for call in calls), 2)

    def test_half_made_folder_is_not_configured(self):
        (self.root / 'build' / 'linux-config' / 'clang').mkdir(parents=True)
        self.configure('msvc-config')
        self.assertEqual(self.run_check()[0], 'xmake-linux.sh f')

    def test_configured_skips_configure(self):
        self.configure('linux-config')
        self.configure('msvc-config')
        self.assertFalse(any(' f' in call for call in self.run_check()))

    def test_no_dll_leaves_windows_alone(self):
        self.assertFalse(any('msvc' in call for call in self.run_check('--no-dll')))

    def test_failed_configure_is_reported_and_builds_nothing(self):
        calls = self.run_check(stub_f=1)
        self.assertFalse(any('build' in call for call in calls))
        self.assertIn('FAIL', self.output)
        self.assertIn('check: FAILED (configure Linux', self.output)

    def test_step_name_prints_before_its_result(self):
        self.run_check()
        self.assertLess(self.output.index('configure Linux          ...'),
                        self.output.index('configure Linux          ok'))


if __name__ == '__main__':
    unittest.main()
