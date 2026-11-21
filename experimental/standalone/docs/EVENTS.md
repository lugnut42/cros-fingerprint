# Linux fingerprint event boundary

Implemented 2026-09-28. This is an offline-tested subscription and discard
primitive, not a completed controller cleanup or enrollment workflow. It is
not linked into the diagnostic or preparation CLI and has not run on hardware.

## Interface

`include/cros_fp_events.h` / `src/events.c` expose `cfp_events_open` and
`cfp_events_drain`. A trusted caller supplies a freshly opened, exclusively owned
descriptor and keeps ownership until it closes that descriptor. No sharing,
flag changes, mask changes, or concurrent reads are allowed during this lifetime.
The caller retains responsibility for device identity/ownership validation.

Open validates O_NONBLOCK and selects only fingerprint events through the
scalar event-mask ioctl. The ioctl argument is the mask value, not a pointer.
It sends no controller host command. Failure requires closing the descriptor;
there is no implicit reopening, retry, or fallback to legacy reads.

Drain reads and discards complete five-byte fingerprint events until EAGAIN or
EWOULDBLOCK. It requests six bytes to detect oversized records even when the
kernel truncates a record. Zero/short/oversized reads and unexpected event types
fail closed. EINTR and other read errors return immediately with saved errno.
Payloads are cleared locally and never returned, printed, or interpreted as an
authentication result.

The caller supplies a read-attempt budget in 1..1024, including the final empty
read. Exhausting it invalidates the handle; consuming exactly the budget without
observing EAGAIN is not success. Failures invalidate the handle. The budget
bounds attempts, not syscall duration: use an outer supervisor as with the
command transport.

## What remains unproven

An empty descriptor queue is only an instantaneous observation. It does not
prove firmware has stopped, that in-flight events have arrived, or that a future
event belongs to a new operation. Queue overflow may silently lose events in the
reviewed kernel. Closing a descriptor does not establish controller cleanup.
Do not use this primitive alone to enable matching or recover an unknown seed.

Before integrating it, resolve mode shutdown and completion semantics against
the label-matching firmware, confirm host event delivery ordering, and define
failure behavior for overflow, suspend, disconnect, cancellation, and reset.
Keep ownership across shutdown, context setup, event draining, and the operation.
The required order and proof of quiescence are still open; an arbitrary delay or
repeated EAGAIN is not that proof. No reset command was selected or executed.

## Validation

`make test` includes `test-events`. Injectable flags/ioctl/read operations cover
subscription ABI, required nonblocking mode, empty/multiple queues, exact budget
exhaustion, the maximum budget, malformed lengths/types, syscall failures,
invalid arguments, and refusal to reuse a failed handle. All fixtures are
synthetic. See TESTING.md for actual red/green and sanitizer results and
SECURITY-SOURCES.md for provenance and the running-kernel limitation.
