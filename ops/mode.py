#!/usr/bin/env python3
"""Host-side DEOS mode configuration, scheduler and Wi-Fi recovery."""

import argparse
import copy
import json
import logging
import os
from pathlib import Path
import re
import socket
import socketserver
import subprocess
import threading
import time

import yaml

CONFIG = Path('/etc/deos/system.yaml')
STATE = Path('/var/lib/deos')
SOCKET = '/run/deos/control.sock'
UPDATE_UNIT = 'deos-bridge-update.service'


def run(*args, timeout=30):
    return subprocess.run(args, check=True, text=True, capture_output=True,
                          timeout=timeout, env=os.environ | {'LC_ALL': 'C'}).stdout.strip()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def validate(data):
    if not isinstance(data, dict) or set(data) != {'mode', 'developer'}:
        raise ValueError('Expected mode and developer sections')
    if data['mode'] not in ('developer', 'prod'):
        raise ValueError('mode must be developer or prod')
    dev = data['developer']
    if not isinstance(dev, dict) or set(dev) != {'bridge_update', 'wifi'}:
        raise ValueError('Expected bridge_update and wifi sections')
    expected = {
        'bridge_update': {'enabled', 'interval_seconds'},
        'wifi': {'enabled', 'interface', 'interval_seconds', 'connect_timeout_seconds',
                 'recover_after_failed_cycles', 'recovery_cooldown_seconds'},
    }
    for name, fields in expected.items():
        section = dev[name]
        if not isinstance(section, dict) or set(section) != fields:
            raise ValueError(f'Invalid fields in {name}')
        if type(section['enabled']) is not bool:
            raise ValueError(f'{name}.enabled must be true or false')
        for field in fields - {'enabled', 'interface'}:
            value = section[field]
            if type(value) is not int or not 1 <= value <= 86400:
                raise ValueError(f'{name}.{field} must be an integer from 1 to 86400')
    if not isinstance(dev['wifi']['interface'], str) or not re.fullmatch(
            r'[a-zA-Z0-9_.-]{1,15}', dev['wifi']['interface']):
        raise ValueError('Invalid Wi-Fi interface')
    return copy.deepcopy(data)


class Wifi:
    def __init__(self, state_dir=STATE, command=run):
        self.path = state_dir / 'wifi-autoconnect.json'
        self.command = command
        self.failures = 0
        self.recovery_path = state_dir / 'wifi-recovery.json'
        self.last_recovery = (json.loads(self.recovery_path.read_text())['last_recovery']
                              if self.recovery_path.exists() else float('-inf'))

    def profiles(self):
        rows = self.command('nmcli', '-t', '-f', 'UUID,TYPE', 'connection', 'show')
        return [row.split(':')[0] for row in rows.splitlines()
                if row.endswith((':802-11-wireless', ':wifi'))]

    def policy(self, prod):
        saved = json.loads(self.path.read_text()) if self.path.exists() else {}
        if prod:
            for uuid in self.profiles():
                if uuid not in saved:
                    saved[uuid] = self.command('nmcli', '-g', 'connection.autoconnect',
                                               'connection', 'show', uuid)
            # Persist originals before modifying profiles, including across power loss.
            save(self.path, saved)
            for uuid in self.profiles():
                self.command('nmcli', 'connection', 'modify', uuid,
                             'connection.autoconnect', 'no')
        elif saved:
            existing = set(self.profiles())
            for uuid, enabled in saved.items():
                if uuid in existing:
                    self.command('nmcli', 'connection', 'modify', uuid,
                                 'connection.autoconnect', enabled)
            self.path.unlink()

    def connected_or_connecting(self, interface):
        state = self.command('nmcli', '-g', 'GENERAL.STATE', 'device', 'show', interface)
        number = int(state.split()[0])
        return 40 <= number <= 100

    def cycle(self, config, allowed):
        try:
            return self.connect(config, allowed)
        except (subprocess.SubprocessError, OSError) as error:
            logging.warning('Wi-Fi control failed: %s', error)
            return self.failed_cycle(config, allowed)

    def connect(self, config, allowed):
        interface = config['interface']
        if self.connected_or_connecting(interface):
            self.failures = 0
            return 'Connected or connecting'
        rows = self.command('nmcli', '-t', '--escape', 'no', '-f', 'SSID,SIGNAL',
                            'device', 'wifi', 'list', 'ifname', interface, '--rescan', 'yes')
        visible = {}
        for row in rows.splitlines():
            ssid, signal = row.rsplit(':', 1)
            visible[ssid] = max(visible.get(ssid, 0), int(signal))
        candidates = []
        for uuid in self.profiles():
            ssid = self.command('nmcli', '-g', '802-11-wireless.ssid', 'connection', 'show', uuid)
            priority = int(self.command('nmcli', '-g', 'connection.autoconnect-priority',
                                        'connection', 'show', uuid))
            if ssid in visible:
                candidates.append((-priority, -visible[ssid], uuid))
        for _, _, uuid in sorted(candidates):
            if not allowed():
                return 'Cancelled by mode change'
            if self.connected_or_connecting(interface):
                self.failures = 0
                return 'Connected or connecting'
            try:
                self.command('nmcli', '--wait', str(config['connect_timeout_seconds']),
                             'connection', 'up', 'uuid', uuid, 'ifname', interface,
                             timeout=config['connect_timeout_seconds'] + 5)
                self.failures = 0
                return f'Connected profile {uuid}'
            except (subprocess.SubprocessError, OSError):
                logging.warning('Wi-Fi profile %s connection failed', uuid)
        return self.failed_cycle(config, allowed)

    def failed_cycle(self, config, allowed):
        interface = config['interface']
        self.failures += 1
        now = time.time()
        if (allowed() and self.failures >= config['recover_after_failed_cycles']
                and now - self.last_recovery >= config['recovery_cooldown_seconds']
                and not self.connected_or_connecting(interface)):
            self.last_recovery = now
            save(self.recovery_path, {'last_recovery': now})
            try:
                self.command('nmcli', 'device', 'set', interface, 'managed', 'no')
            finally:
                self.command('nmcli', 'device', 'set', interface, 'managed', 'yes')
            self.failures = 0
            return 'Wi-Fi interface management re-enabled'
        return f'No connection, failed cycles {self.failures}'


class Manager:
    def __init__(self, state_dir=STATE, wifi=None):
        self.state_dir = state_dir
        self.wifi = wifi or Wifi(state_dir)
        self.lock = threading.RLock()
        self.wifi_lock = threading.Lock()
        self.config = None
        self.generation = 0
        self.jobs = {name: {'running': False, 'last_result': 'Not run', 'next': 0}
                     for name in ('bridge_update', 'wifi')}

    def enabled(self, name):
        with self.lock:
            return (self.config is not None and self.config['mode'] == 'developer'
                    and self.config['developer'][name]['enabled'])

    def apply(self, data):
        config = validate(data)
        with self.wifi_lock, self.lock:
            previous = self.config
            try:
                self.wifi.policy(config['mode'] == 'prod')
                save(self.state_dir / 'applied.json', config)
            except Exception:
                self.wifi.policy(previous is not None and previous['mode'] == 'prod')
                raise
            self.config = config
            self.generation += 1
            self.wifi.failures = 0
            for job in self.jobs.values():
                job['next'] = time.monotonic()
            logging.info('Applied mode %s with settings %s', config['mode'], config['developer'])
        return self.status()

    def status(self):
        with self.lock:
            return {'config': copy.deepcopy(self.config), 'jobs': {
                name: {key: value for key, value in job.items() if key != 'next'}
                for name, job in self.jobs.items()}}

    def work(self, name, config, generation):
        result = 'Completed'
        try:
            if name == 'bridge_update':
                with self.lock:
                    if not self.enabled(name) or generation != self.generation:
                        return
                    # systemd retains ownership when this manager reloads or stops.
                    run('systemctl', 'start', '--no-block', UPDATE_UNIT)
                while run('systemctl', 'show', '-p', 'ActiveState', '--value', UPDATE_UNIT) in (
                        'activating', 'active', 'deactivating'):
                    time.sleep(1)
                result = run('systemctl', 'show', '-p', 'Result', '--value', UPDATE_UNIT)
                if result != 'success':
                    raise RuntimeError(f'Bridge update failed: {result}')
            else:
                with self.wifi_lock:
                    allowed = lambda: self.enabled('wifi') and generation == self.generation
                    if allowed():
                        result = self.wifi.cycle(config, allowed)
        except Exception as error:
            result = f'Failed: {error}'
            logging.exception('%s failed', name)
        finally:
            with self.lock:
                job = self.jobs[name]
                job['running'] = False
                job['last_result'] = result
                job['last_finished'] = time.time()
                job['next'] = time.monotonic() + self.config['developer'][name]['interval_seconds']
            logging.info('%s: %s', name, result)

    def tick(self):
        with self.lock:
            for name, job in self.jobs.items():
                if self.enabled(name) and not job['running'] and time.monotonic() >= job['next']:
                    job['running'] = True
                    threading.Thread(target=self.work, args=(name,
                        copy.deepcopy(self.config['developer'][name]), self.generation),
                        daemon=True).start()


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        self.connection.settimeout(60)
        try:
            request = json.loads(self.rfile.readline(65537))
            if request['command'] == 'apply':
                import struct
                uid = struct.unpack('3i', self.connection.getsockopt(
                    socket.SOL_SOCKET, socket.SO_PEERCRED, 12))[1]
                if uid != 0:
                    raise PermissionError('Run apply with sudo')
                result = self.server.manager.apply(request['config'])
            elif request['command'] == 'status':
                result = self.server.manager.status()
            else:
                raise ValueError('Unknown command')
            response = {'ok': True, 'result': result}
        except Exception as error:
            response = {'ok': False, 'error': str(error)}
        self.wfile.write((json.dumps(response) + '\n').encode())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['serve', 'apply', 'status'])
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
    if args.command == 'serve':
        manager = Manager()
        applied = STATE / 'applied.json'
        config = json.loads(applied.read_text()) if applied.exists() else yaml.safe_load(CONFIG.read_text())
        manager.apply(config)
        Path(SOCKET).unlink(missing_ok=True)
        with socketserver.ThreadingUnixStreamServer(SOCKET, Handler) as server:
            server.manager = manager
            os.chmod(SOCKET, 0o666)
            server.timeout = 1
            while True:
                server.handle_request()
                manager.tick()
    else:
        request = {'command': args.command}
        if args.command == 'apply':
            request['config'] = validate(yaml.safe_load(CONFIG.read_text()))
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(180)
            client.connect(SOCKET)
            client.sendall((json.dumps(request) + '\n').encode())
            response = json.loads(client.makefile().readline())
        if not response['ok']:
            raise SystemExit(response['error'])
        print(yaml.safe_dump(response['result'], sort_keys=False))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, yaml.YAMLError) as error:
        raise SystemExit(str(error))
