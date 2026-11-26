#!/usr/bin/env python3
"""Deploy a CI-approved bridge commit with its own immutable configuration."""

import fcntl
import json
import logging
import os
from pathlib import Path
import signal
import subprocess
import time
import urllib.request

BASE = Path('/opt/deos/bridge-updater')
REPOSITORY = 'DEUDEOS-Team/deos-ros2-bridge'
CONTAINER = 'deos-ros-bridge'
BACKUP = 'deos-ros-bridge-previous'


def run(*args, capture=False, **kwargs):
    result = subprocess.run(args, check=True, text=True, capture_output=capture, **kwargs)
    return result.stdout.strip() if capture else None


def ci_approved(revision):
    url = (f'https://api.github.com/repos/{REPOSITORY}/actions/workflows/ci.yml/runs'
           f'?head_sha={revision}&event=push&per_page=20')
    request = urllib.request.Request(url, headers={
        'Accept': 'application/vnd.github+json', 'User-Agent': 'DEOS-Bridge-Updater'})
    with urllib.request.urlopen(request, timeout=30) as response:
        runs = json.load(response)['workflow_runs']
    matching = [item for item in runs if item['head_sha'] == revision
                and item['head_branch'] == 'main' and item['event'] == 'push']
    if not matching:
        logging.info('Waiting for Bridge CI for %s', revision)
        return False
    latest = max(matching, key=lambda item: item['id'])
    if latest['status'] != 'completed':
        logging.info('Bridge CI is %s for %s', latest['status'], revision)
        return False
    if latest['conclusion'] != 'success':
        raise RuntimeError(f"Bridge CI rejected {revision}: {latest['conclusion']}")
    logging.info('Bridge CI approved %s', revision)
    return True


def archive_repository(repository, destination, revision):
    destination.mkdir(parents=True, exist_ok=True)
    with subprocess.Popen(['git', '-C', str(repository), 'archive', revision],
                          stdout=subprocess.PIPE) as archive:
        try:
            subprocess.run(['tar', '-x', '-C', str(destination)], stdin=archive.stdout, check=True)
        finally:
            archive.stdout.close()
        if archive.wait() != 0:
            raise RuntimeError('Failed to archive release')
    tree = run('git', '-C', str(repository), 'ls-tree', '-rz', revision, capture=True)
    for record in tree.split('\0'):
        if record.startswith('160000 '):
            metadata, path = record.split('\t', 1)
            sub_revision = metadata.split()[2]
            archive_repository(repository / path, destination / path, sub_revision)


def snapshot(source, release, revision):
    if release.exists():
        if (release / '.revision').read_text().strip() != revision:
            raise RuntimeError('Release revision mismatch')
        return
    staging = release.with_name(release.name + '.staging')
    if staging.exists():
        import shutil
        shutil.rmtree(staging)
    archive_repository(source, staging, revision)
    (staging / '.revision').write_text(revision + '\n')
    staging.rename(release)


def inspect(name):
    result = subprocess.run(['docker', 'container', 'inspect', name],
                            text=True, capture_output=True)
    if result.returncode:
        if 'No such' in result.stderr:
            return None
        raise RuntimeError(result.stderr)
    return json.loads(result.stdout)[0]


def wait_healthy():
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        item = inspect(CONTAINER)
        if item is None or not item['State']['Running'] or item['RestartCount']:
            raise RuntimeError('Bridge stopped or restarted during deployment')
        health = item['State'].get('Health', {}).get('Status')
        if health == 'healthy':
            time.sleep(5)
            item = inspect(CONTAINER)
            if (item and item['State']['Running'] and item['RestartCount'] == 0
                    and item['State'].get('Health', {}).get('Status') == 'healthy'):
                return
            raise RuntimeError('Bridge failed stability check')
        if health == 'unhealthy':
            raise RuntimeError('Bridge healthcheck failed')
        time.sleep(2)
    raise RuntimeError('Bridge healthcheck timed out')


def deploy(release, revision, base=BASE):
    env = os.environ | {'BRIDGE_IMAGE': f'deos-ros-bridge:main-{revision}',
                        'BRIDGE_REVISION': revision}
    compose = ['docker', 'compose', '--env-file', '/dev/null', '-p',
               f'deos-bridge-{revision[:12]}', '-f', str(release / 'deploy/compose.yaml')]
    # Reject invalid manifests and build failures before stopping the current bridge.
    run(*compose, 'config', '--quiet', env=env)
    run(*compose, 'build', '--pull', 'bridge', env=env)
    old = inspect(CONTAINER)
    renamed = False
    switched = False
    try:
        if old:
            if inspect(BACKUP):
                run('docker', 'rm', '-f', BACKUP)
            run('docker', 'rename', CONTAINER, BACKUP)
            renamed = True
            switched = True
            run('docker', 'update', '--restart=no', BACKUP)
            run('docker', 'stop', '-t', '10', BACKUP)
        else:
            switched = True
        run(*compose, 'up', '-d', '--no-build', '--pull', 'never', 'bridge', env=env)
        wait_healthy()
        temporary = base / 'deployed-revision.tmp'
        temporary.write_text(revision + '\n')
        temporary.replace(base / 'deployed-revision')
        logging.info('Deployed code, Compose and parameters from %s', revision)
    except BaseException:
        if switched:
            subprocess.run(['docker', 'logs', '--tail', '60', CONTAINER], check=False)
            subprocess.run(['docker', 'rm', '-f', CONTAINER], check=False)
            if renamed:
                run('docker', 'rename', BACKUP, CONTAINER)
                policy = old['HostConfig']['RestartPolicy']
                restart = policy['Name'] or 'no'
                if restart == 'on-failure' and policy.get('MaximumRetryCount'):
                    restart += f":{policy['MaximumRetryCount']}"
                run('docker', 'update', f'--restart={restart}', CONTAINER)
                if old['State']['Running']:
                    run('docker', 'start', CONTAINER)
                logging.warning('Restored previous container and its release configuration')
        raise


def main():
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
    BASE.mkdir(parents=True, exist_ok=True)
    with (BASE / 'update.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return
        source = BASE / 'source'
        if not (source / '.git').exists():
            run('git', 'clone', '--no-checkout', f'https://github.com/{REPOSITORY}.git', str(source))
        git_env = os.environ | {'GIT_TERMINAL_PROMPT': '0'}
        run('git', '-C', str(source), 'fetch', 'origin', 'main', env=git_env, timeout=120)
        revision = run('git', '-C', str(source), 'rev-parse', 'FETCH_HEAD', capture=True)
        marker = BASE / 'deployed-revision'
        if marker.exists() and marker.read_text().strip() == revision:
            logging.info('Main unchanged: %s', revision)
            return
        if not ci_approved(revision):
            return
        run('git', '-C', str(source), 'checkout', '--detach', revision)
        run('git', '-C', str(source), 'submodule', 'sync', '--recursive')
        run('git', '-C', str(source), 'submodule', 'update', '--init', '--recursive',
            env=git_env, timeout=300)
        run('bash', str(source / 'ops/test-unit.sh'))
        release = BASE / 'releases' / revision
        snapshot(source, release, revision)
        deploy(release, revision)


if __name__ == '__main__':
    def interrupted(signum, frame):
        raise RuntimeError(f'Updater interrupted by signal {signum}')

    signal.signal(signal.SIGTERM, interrupted)
    main()
