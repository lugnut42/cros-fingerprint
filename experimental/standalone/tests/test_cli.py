# SPDX-License-Identifier: LGPL-2.1-or-later
import json
from pathlib import Path
import subprocess
import sys
import tempfile

build = Path(sys.argv[1]).resolve()
fixture = subprocess.run([build / 'test-json'], capture_output=True, text=True, check=True)
data = json.loads(fixture.stdout)
assert set(data) == {'schema_version', 'info_version', 'sensor', 'encryption'}
assert data['schema_version'] == 1 and data['info_version'] == 3
assert data['sensor']['model'] == 543
assert data['sensor']['dead_pixels'] is None
assert data['encryption']['seed_set'] is None
assert len(data['sensor']['formats']) == 1
assert data['sensor']['formats'][0]['image_data_offset'] == 400
cli = build / 'cros-fingerprint-diagnostic'
with tempfile.TemporaryDirectory() as directory:
    missing = Path(directory) / 'missing'
    result = subprocess.run([cli, '--device', missing], capture_output=True, text=True, timeout=5)
    assert result.returncode == 1 and not result.stdout
    assert 'No such file' in result.stderr
    result = subprocess.run([cli, '--mode-status', '--device', missing], capture_output=True, text=True, timeout=5)
    assert result.returncode == 1 and not result.stdout
    assert 'No such file' in result.stderr
    result = subprocess.run([cli, '--mode-status', '--device', '/dev/null'], capture_output=True, text=True, timeout=5)
    assert result.returncode == 1 and not result.stdout and 'ioctl' in result.stderr.lower()
    regular = Path(directory) / 'regular'
    regular.touch()
    result = subprocess.run([cli, '--device', regular], capture_output=True, text=True, timeout=5)
    assert result.returncode == 1 and not result.stdout
    assert 'character device' in result.stderr
    result = subprocess.run([cli, '--device', '/dev/null'], capture_output=True, text=True, timeout=5)
    assert result.returncode == 1 and not result.stdout
    assert 'ioctl' in result.stderr.lower()
result = subprocess.run([cli, '--unknown'], capture_output=True, text=True)
assert result.returncode == 2 and not result.stdout
print('JSON and CLI checks passed')
