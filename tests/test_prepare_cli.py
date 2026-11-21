# SPDX-License-Identifier: LGPL-2.1-or-later
import json
from pathlib import Path
import subprocess
import sys
cli = Path(sys.argv[1]).resolve() / 'cros-fingerprint-prepare'
for args in ([], ['--unknown'], ['--prepare'], ['--prepare', '-1', 'alice'],
             ['--prepare', '1x', 'alice'], ['--prepare', '18446744073709551616', 'alice'],
             ['--prepare', '1000', '../alice'], ['--prepare', '1000', ''],
             ['--prepare', '1000', 'x' * 64], ['--plan', 'extra']):
    result = subprocess.run([cli, *args], capture_output=True, text=True, timeout=2)
    assert result.returncode == 2 and not result.stdout, (args, result)
result = subprocess.run([cli, '--plan'], capture_output=True, text=True, timeout=2)
assert result.returncode == 0
plan = json.loads(result.stdout)
assert plan['state_directory'] == '/var/lib/cros-fingerprint-prototype'
assert plan['device'] == '/dev/cros_fp'
assert plan['state_changing_commands'] == ['FP_SEED', 'FP_CONTEXT']
assert plan['create_state'] is False and plan['capture'] is False
assert plan['deadline_ms'] == 5000
print('Account preparation CLI non-executing checks passed')
