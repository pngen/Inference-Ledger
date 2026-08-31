# ARCHITECTURE

Inference Ledger is an exact, replayable inference-accounting runtime. It
observes and attributes consumption; it never decides admission, scheduling,
quota, placement or transfer policy.

## Modules

- `identity`: strongly typed 128-bit identities and monotonic generations.
- `unit`: quantity model with explicit units and provenance.
- `ledger_entry` / `ledger`: immutable append-only ledger with idempotency and
  authority fencing.
- `codec`: deterministic, checksummed binary encoding (big-endian, sparse fields).
- `request_account`: canonical per-request reconciliation.
- `batch`: batch tracking and deterministic shared-cost attribution.
- `pricing`: typed, versioned pricing policies and deterministic recompute.
- `persistence`: strict versioned persistence, snapshot, append, replay, recover.
- `protocol` / `coordinator` / `worker`: framed TCP distributed accounting with
  authority roll.
- `query`: ledger queries and deterministic aggregates.
- CLI, benchmarks, examples and proofs.

## Accounting doctrine

Every value carries provenance (measured, reported, derived, estimated,
reconstructed, unavailable). Physical consumption, allocation, reservation,
useful work, wasted work, avoided work and amortized shared cost are kept
distinct. Nothing is invented; missing measurements stay missing.

## Authority model

Mutable accounting is fenced by CoordinatorEpoch, WorkerBootId,
RequestGeneration, AttemptId, AttemptGeneration, AccountingGeneration and
DispatchId. A stale completion (old boot, old epoch, old generation) is rejected
before it can mutate current totals. Duplicate events are idempotent.

## Replay

Persistence stores immutable frames with per-frame CRC. Replay reproduces the
same physical totals and the same ledger fingerprint. A corrupted, truncated or
duplicate-id frame is rejected.

## CUDA

A real CUDA proof (RTX 5090 / sm_120) performs actual cudaMalloc, H2D/D2H,
prefill/decode kernels and emits ledger events from measured timings, then
proves allocations return to baseline.
