# SPDX-License-Identifier: LGPL-2.1-or-later
import fcntl
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

build = Path(sys.argv[1]).resolve()
production = build / 'cros-fingerprint-admin'
driver = build / 'test-admin-driver'

def run(cli, args, root=None, fault=None):
    env = os.environ.copy()
    env.pop('CFP_TEST_ROOT', None)
    env.pop('CFP_TEST_FAULT', None)
    if root is not None:
        env['CFP_TEST_ROOT'] = str(root)
    if fault:
        env['CFP_TEST_FAULT'] = fault
    return subprocess.run([cli, *args], env=env, capture_output=True, text=True, timeout=8)

def expect(args, root, code=0, fault=None):
    result = run(driver, args, root, fault)
    assert result.returncode == code, (args, result.returncode, result.stderr)
    if code == 0:
        # Exact allowlist: no account identifiers, seeds, or contexts in output.
        assert json.loads(result.stdout) == {
            'action': args[0][2:], 'status': 'ok', 'hardware_access': False}
        assert not result.stderr
    else:
        assert not result.stdout
    return result

# Production binary is only used for non-executing plan/argument/root checks.
plan = run(production, ['--plan'])
assert plan.returncode == 0, plan
assert json.loads(plan.stdout) == {
    'state_directory': '/var/lib/cros-fingerprint-prototype',
    'actions': ['provision', 'register', 'check', 'check-account'],
    'hardware_access': False, 'deadline_ms': 5000}
for args in ([], ['--bad'], ['--provision', 'extra'], ['--register'],
             ['--register', '-1', 'alice'], ['--register', '1x', 'alice'],
             ['--register', '18446744073709551616', 'alice'],
             ['--register', '1000', '../alice'], ['--register', '1000', ''],
             ['--register', '1000', 'x' * 64], ['--plan', 'extra'],
             ['--retire', '1000', 'alice'], ['--root', '/tmp'],
             ['--check-account', '1000'], ['--check', 'extra']):
    result = run(production, args)
    assert result.returncode == 2 and not result.stdout, (args, result)
if os.geteuid() != 0:
    for args in (['--provision'], ['--register', '1000', 'alice'], ['--check']):
        result = run(production, args)
        assert result.returncode == 1 and not result.stdout, (args, result)

with tempfile.TemporaryDirectory(prefix='cfp-admin-') as tmp:
    root = Path(tmp)
    expect(['--check'], root, 1)
    expect(['--register', '1000', 'alice'], root, 1)
    expect(['--provision'], root, 1, 'unauthorized')
    assert not list(root.iterdir())
    expect(['--provision'], root)
    state = root / 'state'
    record = state / 'provision/provision'
    before = record.read_bytes()  # Synthetic fixture only; never print it.
    expect(['--provision'], root, 1)
    expect(['--check'], root)
    expect(['--register', '1001', 'alice'], root, 1)
    expect(['--register', '1000', 'absent'], root, 1)
    expect(['--check-account', '1000', 'alice'], root, 1)
    expect(['--register', '1000', 'alice'], root)
    registry = state / 'accounts/registry'
    registered = registry.read_bytes()
    expect(['--register', '1000', 'alice'], root, 1)
    expect(['--check-account', '1000', 'alice'], root)
    assert registry.read_bytes() == registered
    expect(['--register', '1001', 'bob'], root)
    expect(['--check-account', '1001', 'bob'], root)
    expect(['--check-account', '1000', 'alice'], root)
    assert record.read_bytes() == before
    for p in state.rglob('*'):
        assert stat.S_IMODE(p.stat().st_mode) == (0o700 if p.is_dir() else 0o600)
    fd = os.open(state, os.O_RDONLY | os.O_DIRECTORY)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        expect(['--check'], root, 1)
        expect(['--register', '1000', 'alice'], root, 1)
    finally:
        os.close(fd)
    expect(['--check'], root)
    expect(['--register', '1000', 'alice'], root, 124, 'lookup-hang')
    expect(['--check'], root)
    record.write_bytes(before[:-1])
    expect(['--check'], root, 1)
    expect(['--register', '1000', 'alice'], root, 1)
    expect(['--provision'], root, 1)
    assert record.read_bytes() == before[:-1]

for fault in ('random', 'write', 'sync', 'write-hang'):
    with tempfile.TemporaryDirectory(prefix='cfp-admin-failure-') as tmp:
        root = Path(tmp)
        expect(['--provision'], root, 124 if fault == 'write-hang' else 1, fault)
        assert (root / 'state').is_dir()
        expect(['--provision'], root, 1)
        expect(['--check'], root, 1)

with tempfile.TemporaryDirectory(prefix='cfp-admin-race-') as tmp:
    root = Path(tmp)
    env = os.environ.copy()
    env['CFP_TEST_ROOT'] = str(root)
    env.pop('CFP_TEST_FAULT', None)
    workers = [subprocess.Popen([driver, '--provision'], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True) for _ in range(2)]
    outputs = [worker.communicate(timeout=8) for worker in workers]
    assert sorted(worker.returncode for worker in workers) == [0, 1]
    for worker, (stdout, stderr) in zip(workers, outputs):
        if worker.returncode:
            assert not stdout
        else:
            assert json.loads(stdout) == {
                'action': 'provision', 'status': 'ok', 'hardware_access': False}
    expect(['--check'], root)

with tempfile.TemporaryDirectory(prefix='cfp-admin-partial-') as tmp:
    root = Path(tmp)
    expect(['--provision'], root)
    registry = root / 'state/accounts/registry'
    before = registry.read_bytes()
    expect(['--register', '1000', 'alice'], root, 1, 'write')
    assert registry.read_bytes() == before
    assert (registry.parent / '.registry.pending').exists()
    expect(['--check'], root, 1)
    expect(['--register', '1000', 'alice'], root, 1)

with tempfile.TemporaryDirectory(prefix='cfp-admin-hang-') as tmp:
    root = Path(tmp)
    expect(['--provision'], root)
    registry = root / 'state/accounts/registry'
    before = registry.read_bytes()
    expect(['--register', '1000', 'alice'], root, 124, 'write-hang')
    assert registry.read_bytes() == before
    assert (registry.parent / '.registry.pending').exists()
    # The killed writer must release both ownership locks. Pending evidence
    # still blocks ordinary use, so reopening successfully is not expected.
    for path in (root / 'state', registry.parent):
        fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            os.close(fd)
    expect(['--check'], root, 1)

for kind in ('symlink', 'permissive', 'existing-empty'):
    with tempfile.TemporaryDirectory(prefix='cfp-admin-path-') as tmp:
        root = Path(tmp)
        target = root / 'state'
        if kind == 'symlink':
            (root / 'target').mkdir(mode=0o700)
            target.symlink_to(root / 'target', target_is_directory=True)
        else:
            target.mkdir(mode=0o700)
            if kind == 'permissive':
                target.chmod(0o755)
        expect(['--provision'], root, 1)
        expect(['--check'], root, 1)
        assert not list(target.iterdir())
print('Administrative CLI offline checks passed')
