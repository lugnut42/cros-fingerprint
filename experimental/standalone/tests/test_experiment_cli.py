# SPDX-License-Identifier: LGPL-2.1-or-later
import json
from pathlib import Path
import subprocess
import sys
cli = Path(sys.argv[1]).resolve() / 'cros-fingerprint-seed-context-test'
for arguments in ([], ['--unknown'], ['--initialize', '--restore']):
    result = subprocess.run([cli, *arguments], capture_output=True, text=True, timeout=2)
    assert result.returncode == 2 and not result.stdout
plan = subprocess.run([cli, '--plan'], capture_output=True, text=True, timeout=2)
assert plan.returncode == 0
record = json.loads(plan.stdout)
assert record['device'] == '/dev/cros_fp'
assert record['state_directory'] == '/var/lib/cros-fingerprint-experiment'
assert record['state_changing_commands'] == ['FP_SEED', 'FP_CONTEXT']
assert record['capture'] is False and record['enroll'] is False
assert record['deadline_ms'] == 5000
print('Experiment CLI non-executing checks passed')
