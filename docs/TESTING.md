# Testing the driver

The five `tests/test_reference_*.py` scripts check the `crfpmoc` driver
sources offline. They extract the real driver code, compile it against fake
transports and synthetic replies, and need no reader, root, network or real
biometric data. They need Python 3, a C compiler and GLib/GIO development
headers.

The target is a clone of the libfprint fork (`https://github.com/lugnut42/libfprint`,
branch `crfpmoc`; the packaging recipe builds from it). Here `../libfprint` is a
sibling clone:

```sh
D=../libfprint/libfprint/drivers/crfpmoc

python3 tests/test_reference_logging.py  $D/crfpmoc-ec-transfer.c
python3 tests/test_reference_replies.py  $D/crfpmoc-ec-transfer.c
python3 tests/test_reference_fp_info.py  $D
python3 tests/test_reference_live_fixes.py $D
python3 tests/test_reference_keys.py $D
```

Each prints `... PASS` and exits 0, or exits non-zero with a `FAIL` message.

| Test | Covers |
| --- | --- |
| `test_reference_logging.py` | The transfer helper's command logger, compiled with instrumented logging sinks. |
| `test_reference_replies.py` | The transfer helper's reply handling with synthetic fixed-size and variable-length replies: short, exact, oversized and error. |
| `test_reference_fp_info.py` | FP_INFO v1/v2/v3 negotiation and bounded variable-length decoding. |
| `test_reference_live_fixes.py` | Enroll result codes stored regardless of log level; FP_SEED sent exactly when the encryption status shows the seed bit clear; sensor idled (FP_MODE 0) before FP_CONTEXT. Applies to the `crfpmoc` branch only: one check targets code that exists only with the acd407 commits. |
| `test_reference_keys.py` | Machine key file: creation, strict validation, never replacing an invalid file; the stale-seed refusal; decrypt-failure error mapping. |

## Rules for changing driver behaviour

Write a failing test first, using synthetic data only, and confirm it fails for
the intended reason. Then make the smallest change that passes. Never use a
person's fingerprint image or template as a fixture.

## Live smoke test

`scripts/smoke-test.sh` runs the driver's enroll and verify examples from a
libfprint clone's build tree against a real reader (needs the reader, sudo, and
a build in the clone: `meson setup _build -Ddrivers=all -Dintrospection=false
-Ddoc=false && meson compile -C _build`; default path
`../libfprint/_build/examples`, override with `FP_EXAMPLES_DIR`; stop fprintd
first). It is manual and not
part of any automated run.
