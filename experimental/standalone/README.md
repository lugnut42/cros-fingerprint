# Standalone groundwork (experimental, not used)

This directory holds the first approach to the ChromeOS fingerprint reader: a
standalone C11 implementation of the FPMCU protocol, a read-only hardware
diagnostic, and a design for a per-machine seed store, account registry and
admin tools. It was never finished.

**The working setup does not use any of this.** It uses the `crfpmoc`
libfprint driver with stock fprintd; see the top-level README. The design
documents in `docs/` describe the abandoned approach and are kept as history
(`docs/HANDOFF.md` is the old session log).

## What is still useful: the read-only diagnostic

`cros-fingerprint-diagnostic` queries firmware info, command-version support,
sensor metadata and encryption status, and prints one JSON object. It changes
nothing on the reader.

```sh
cd experimental/standalone
make build/cros-fingerprint-diagnostic
pkexec "$PWD/build/cros-fingerprint-diagnostic"      # /dev/cros_fp is root-only
```

`--mode-status` reports the configured FP mode (read-only), and `--device PATH`
selects another device node. Needs a C11 compiler and Make.

## Tests

```sh
cd experimental/standalone
make test      # offline; needs a C compiler, Make, Python 3 and OpenSSL headers
```

`BUILD` defaults to `build/` inside this directory. No reader or root is needed.

## WARNING: do not run the seed tools on a machine using the working setup

The other executables (`cros-fingerprint-prepare --prepare`, the seed/context
experiment and its `--restore`, and the admin tool) **program a different seed
into the reader**. Fingerprints enrolled while that seed is set stop working
after the next reboot, and the working setup's prints will not match. Only the
diagnostic and `make test` are safe to run.
