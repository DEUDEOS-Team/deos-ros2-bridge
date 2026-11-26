import copy
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('mode', ROOT / 'mode.py')
mode = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mode)
DEFAULT = yaml.safe_load((ROOT / 'system.yaml').read_text())


class Network:
    def __init__(self):
        self.calls = []
        self.connected = False
        self.fail = False
        self.auto = {'a': 'yes', 'b': 'no'}

    def __call__(self, *args, **kwargs):
        self.calls.append(args)
        if args[1:5] == ('-t', '-f', 'UUID,TYPE', 'connection'):
            return 'a:802-11-wireless\nb:802-11-wireless'
        if args[1:3] == ('-g', 'GENERAL.STATE'):
            return '100 (connected)' if self.connected else '30 (disconnected)'
        if 'SSID,SIGNAL' in args:
            return 'alpha:90\nbeta:40'
        if args[1:3] == ('-g', 'connection.autoconnect'):
            return self.auto[args[-1]]
        if args[1:3] == ('-g', '802-11-wireless.ssid'):
            return {'a': 'alpha', 'b': 'beta'}[args[-1]]
        if args[1:3] == ('-g', 'connection.autoconnect-priority'):
            return {'a': '0', 'b': '10'}[args[-1]]
        if args[1:3] == ('connection', 'modify'):
            self.auto[args[3]] = args[-1]
        if 'up' in args:
            if self.fail:
                raise subprocess.CalledProcessError(1, args)
            self.connected = True
        return ''


class ModeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.network = Network()
        self.wifi = mode.Wifi(self.path, self.network)

    def test_validation_rejects_unsafe_input(self):
        for field, value in [('interval_seconds', 0), ('enabled', 'yes'), ('interface', 'wlan0;reboot')]:
            config = copy.deepcopy(DEFAULT)
            config['developer']['wifi'][field] = value
            with self.assertRaises(ValueError):
                mode.validate(config)

    def test_prod_preserves_connection_and_restores_original_flags_after_restart(self):
        self.network.connected = True
        self.wifi.policy(True)
        self.assertEqual(self.network.auto, {'a': 'no', 'b': 'no'})
        self.assertTrue(self.network.connected)
        mode.Wifi(self.path, self.network).policy(False)
        self.assertEqual(self.network.auto, {'a': 'yes', 'b': 'no'})
        self.assertFalse(any('managed' in call for call in self.network.calls))

    def test_connected_network_is_not_scanned_or_changed(self):
        self.network.connected = True
        self.wifi.cycle(DEFAULT['developer']['wifi'], lambda: True)
        self.assertEqual(len(self.network.calls), 1)

    def test_priority_precedes_signal(self):
        self.wifi.cycle(DEFAULT['developer']['wifi'], lambda: True)
        attempts = [call for call in self.network.calls if 'up' in call]
        self.assertEqual(attempts[0][6], 'b')

    def test_recovery_is_scoped_and_rate_limited(self):
        self.network.fail = True
        config = DEFAULT['developer']['wifi']
        for _ in range(6):
            self.wifi.cycle(config, lambda: True)
        resets = [call for call in self.network.calls if 'managed' in call]
        self.assertEqual(resets, [('nmcli', 'device', 'set', 'wlan0', 'managed', 'no'),
                                 ('nmcli', 'device', 'set', 'wlan0', 'managed', 'yes')])
        restarted = mode.Wifi(self.path, self.network)
        for _ in range(3):
            restarted.cycle(config, lambda: True)
        self.assertEqual(len([call for call in self.network.calls if 'managed' in call]), 2)

    def test_scan_failures_trigger_recovery(self):
        def command(*args, **kwargs):
            if 'SSID,SIGNAL' in args:
                raise subprocess.TimeoutExpired(args, 30)
            return self.network(*args, **kwargs)
        self.wifi.command = command
        for _ in range(3):
            result = self.wifi.cycle(DEFAULT['developer']['wifi'], lambda: True)
        self.assertEqual(result, 'Wi-Fi interface management re-enabled')

    def test_invalid_apply_keeps_config_and_period_changes_apply(self):
        manager = mode.Manager(self.path, self.wifi)
        manager.apply(DEFAULT)
        changed = copy.deepcopy(DEFAULT)
        changed['developer']['wifi']['interval_seconds'] = 12
        manager.apply(changed)
        changed['mode'] = 'invalid'
        with self.assertRaises(ValueError):
            manager.apply(changed)
        self.assertEqual(manager.config['developer']['wifi']['interval_seconds'], 12)
        self.assertEqual(manager.config['mode'], 'developer')
        persisted = mode.json.loads((self.path / 'applied.json').read_text())
        self.assertEqual(persisted, manager.config)

    def test_update_survives_prod_transition_without_overlap(self):
        manager = mode.Manager(self.path, self.wifi)
        manager.apply(DEFAULT)
        self.network.connected = True
        started = threading.Event()
        release = threading.Event()
        starts = []

        def command(*args, **kwargs):
            if args[:2] == ('systemctl', 'start'):
                starts.append(args)
                started.set()
                return ''
            if 'ActiveState' in args:
                release.wait(3)
                return 'inactive'
            return 'success'

        with patch.object(mode, 'run', command):
            manager.tick()
            self.assertTrue(started.wait(2))
            deadline = time.monotonic() + 2
            while manager.jobs['wifi']['running'] and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertEqual(manager.jobs['wifi']['last_result'], 'Connected or connecting')
            manager.tick()
            prod = copy.deepcopy(DEFAULT)
            prod['mode'] = 'prod'
            manager.apply(prod)
            self.assertTrue(manager.jobs['bridge_update']['running'])
            manager.tick()
            self.assertEqual(len(starts), 1)
            release.set()
            deadline = time.monotonic() + 3
            while manager.jobs['bridge_update']['running'] and time.monotonic() < deadline:
                time.sleep(.01)
        self.assertEqual(manager.jobs['bridge_update']['last_result'], 'success')


if __name__ == '__main__':
    unittest.main()
