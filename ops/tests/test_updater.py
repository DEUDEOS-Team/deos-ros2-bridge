import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('updater', ROOT / 'updater.py')
updater = importlib.util.module_from_spec(spec)
spec.loader.exec_module(updater)


class UpdaterTests(unittest.TestCase):
    def ci(self, runs):
        with patch.object(updater.urllib.request, 'urlopen',
                          return_value=io.BytesIO(json.dumps({'workflow_runs': runs}).encode())):
            return updater.ci_approved('abc')

    def record(self, **values):
        return dict(id=1, head_sha='abc', head_branch='main', event='push',
                    status='completed', conclusion='success') | values

    def test_ci_requires_matching_successful_main_push(self):
        self.assertTrue(self.ci([self.record()]))
        self.assertFalse(self.ci([]))
        self.assertFalse(self.ci([self.record(status='in_progress', conclusion=None)]))
        self.assertFalse(self.ci([self.record(head_sha='other')]))
        self.assertFalse(self.ci([self.record(event='pull_request')]))
        with self.assertRaises(RuntimeError):
            self.ci([self.record(), self.record(id=2, conclusion='failure')])

    def test_ci_network_failure_blocks_deployment(self):
        with patch.object(updater.urllib.request, 'urlopen', side_effect=OSError('offline')):
            with self.assertRaises(OSError):
                updater.ci_approved('abc')

    def test_release_contains_committed_config_not_working_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / 'source'
            source.mkdir()
            for args in [('init',), ('config', 'user.email', 'test@example.com'),
                         ('config', 'user.name', 'Test')]:
                subprocess.run(['git', '-C', str(source), *args], check=True, capture_output=True)
            (source / 'params.yaml').write_text('value: original\n')
            subprocess.run(['git', '-C', str(source), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(source), 'commit', '-qm', 'initial'], check=True)
            revision = updater.run('git', '-C', str(source), 'rev-parse', 'HEAD', capture=True)
            (source / 'params.yaml').write_text('value: changed\n')
            release = base / 'releases' / revision
            updater.snapshot(source, release, revision)
            self.assertEqual((release / 'params.yaml').read_text(), 'value: original\n')
            self.assertFalse((release / '.git').exists())

    def test_failed_candidate_restores_previous_container_without_advancing_revision(self):
        calls = []
        old = {'State': {'Running': True},
               'HostConfig': {'RestartPolicy': {'Name': 'unless-stopped'}}}

        def command(*args, **kwargs):
            calls.append(args)

        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            marker = base / 'deployed-revision'
            marker.write_text('old\n')
            with patch.object(updater, 'run', command), \
                    patch.object(updater, 'inspect', side_effect=[old, None]), \
                    patch.object(updater, 'wait_healthy', side_effect=RuntimeError('unhealthy')), \
                    patch.object(updater.subprocess, 'run'):
                with self.assertRaises(RuntimeError):
                    updater.deploy(base, 'abc', base)
            self.assertEqual(marker.read_text(), 'old\n')
        self.assertIn(('docker', 'rename', updater.BACKUP, updater.CONTAINER), calls)
        self.assertIn(('docker', 'start', updater.CONTAINER), calls)

    def test_bad_compose_does_not_stop_running_bridge(self):
        with patch.object(updater, 'run', side_effect=RuntimeError('invalid config')), \
                patch.object(updater, 'inspect') as inspect:
            with self.assertRaises(RuntimeError):
                updater.deploy(Path('/unused'), 'abc')
            inspect.assert_not_called()


if __name__ == '__main__':
    unittest.main()
