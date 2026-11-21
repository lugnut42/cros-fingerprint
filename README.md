# cros-fingerprint

Linux fingerprint support for ChromeOS fingerprint controllers, starting with
the HP Elite Dragonfly Chromebook (board name **Redrix**).

**Status: working on the target machine as of 2026-11-20.** The patched
`crfpmoc` libfprint driver (see `patches/` and `packaging/`) with stock fprintd
enrolls and matches, and fingerprint is enabled for sudo, polkit and the Omarchy
lock screen. It uses the driver's fixed seed/context; this has not had a
security review. See [docs/HANDOFF.md](docs/HANDOFF.md) for exactly what is
installed, how to undo it, and what is still unverified. The standalone C
diagnostic, seed store, registry and admin tools below are earlier groundwork
and are not part of the working setup.

The target machine already exposes `/dev/cros_fp` through existing Linux kernel
drivers. Read-only queries have returned firmware information, command-version
support, and sensor metadata. The initial work is therefore in userspace, with
no demonstrated need for a new kernel driver or firmware changes.

## Start here

- [Current reboot handoff](docs/HANDOFF.md)

- [Development plan and TDD milestones](docs/PLAN.md)
- [Implementation and licensing decisions](docs/DECISIONS.md)
- [Current reuse and security review](docs/REUSE-REVIEW.md)
- [Running tests and the red/green record](docs/TESTING.md)
- [Hardware findings and their limits](docs/HARDWARE.md)
- [Security and lifecycle design](docs/SECURITY-DESIGN.md)
- [Seed/context hardware experiment](docs/SEED-CONTEXT-EXPERIMENT.md)
- [State-directory and session APIs](docs/STATE-LIFECYCLE.md)
- [Persistent account registry](docs/ACCOUNT-REGISTRY.md)
- [Provisioned store and operation ownership](docs/PROVISIONED-STORE.md)
- [Explicit storage administration](docs/ADMIN.md)
- [Proposed first live prototype setup](docs/PROTOTYPE-LIVE-PLAN.md)
- [Security acceptance scenarios](docs/SECURITY-TESTS.md)
- [Security source review](docs/SECURITY-SOURCES.md)
- [Linux event subscription and drain boundary](docs/EVENTS.md)
- [Sources and existing implementations](docs/SOURCES.md)
- [Contributor and agent instructions](AGENTS.md)

## Approach

Build a small protocol layer behind a replaceable transport, test it without
hardware, and establish a reliable read-only diagnostic before attempting
enrollment. Then prove enrollment, verification, deletion, and secure restoration
after reboot before integrating with libfprint/fprintd and the desktop.

The implementation language is C11, with libfprint as the future integration
target. The initial protocol core has no library dependencies. A separate
privileged service remains a possibility if secure secret management cannot
be accommodated appropriately in the existing stack; that decision is deferred
until the security-design milestone.

Tests must not require a fingerprint reader, root privileges, network access,
or real biometric data. Hardware experiments are separate, explicit steps.

## Local setup

Clone into a normal development directory and use a C11 compiler, Make, Python 3 (CLI tests), and OpenSSL development
headers/libcrypto (seed-store tests and experiment command):

```sh
make test
```

No system package replacement, PAM configuration, firmware modification, or
reader access is needed. See [testing](docs/TESTING.md) for sanitizer commands.

## Read-only hardware diagnostic

Build with `make`, then run `./build/cros-fingerprint-diagnostic`. On the target,
`/dev/cros_fp` is accessible only to root; an explicit privileged invocation is:

```sh
pkexec "$PWD/build/cros-fingerprint-diagnostic"
```

The tool queries capabilities, sensor metadata, and encryption status only.
It emits one schema-versioned JSON object on success, and errors on stderr with
no JSON on failure. `--device PATH` selects another character device; symlinks
are rejected. It performs no installation or authentication configuration.

The five-second deadline starts after the program launches (not while waiting
for pkexec authentication). On timeout the parent kills its worker and exits
124; an uninterruptible kernel operation can outlive the kill request. The
supervisor is for a one-shot CLI, not an embeddable long-running service.
Worker failures exit 125; query/access failures exit 1; usage errors exit 2.

## Licensing

New project code is licensed under **LGPL-2.1-or-later**; see [LICENSE](LICENSE).
No third-party driver implementation has been imported. Preserve licenses and
attribution if incorporating external code in the future.

## Read-only configured mode

`./build/cros-fingerprint-diagnostic --mode-status` negotiates FP_MODE v0 and
sends only the exact DONT_CHANGE request. Its small JSON report indicates the
configured mode; `configured_idle` is not a sensor-completion or event-freshness
proof. The default metadata diagnostic output remains unchanged.
