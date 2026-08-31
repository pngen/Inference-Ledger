# Inference Ledger

An exact, replayable inference-accounting runtime. It answers what a request
actually consumed, what useful work that produced, what was reused or wasted,
and what exact cost should be attributed to it.

## Systems boundary

Inference Ledger owns exact, replayable accounting for inference-runtime resource
consumption and useful work. It observes and attributes consumption; it does not
decide admission, scheduling, quota, latency policy, placement, residency or
transfer policy. It does not duplicate Serving Observatory: Inference Ledger is
not a metrics collector, dashboard or billing wrapper — it is the accounting
plumbing.

## Core question

_What resources did this inference request actually consume, what useful work
did those resources produce, what was reused or wasted, and what exact cost
should be attributed to the request?_

## Accounting doctrine

Never invent precision. Every accounting value carries provenance:
measured, reported, derived, estimated, reconstructed or unavailable. Physical
consumption, logical consumption, reserved capacity, allocated capacity, useful
work, wasted work, avoided work, attributed cost and amortized shared cost are
kept distinct. Reservation is never collapsed into consumption; residency is
never collapsed into execution; shared resources are never counted twice.
Missing measurements stay missing.

## Units and provenance

Quantities carry an explicit unit (count, bytes, byte-seconds, seconds,
nanoseconds, joules, Wh, currency, dimensionless) and a provenance tag. The
codec rejects NaN, Inf, negative physical quantities and invalid enum values.

## Ledger model

An append-oriented ledger of immutable records. Each entry carries a stable
identity, request/tenant/workload/model/revision/adapter, attempt, dispatch,
worker/node/device, event kind, resource kind, quantity, unit, timestamps,
provenance, source identity, authority envelope, accounting generation and
optional cost-policy identity and metadata. Event categories include
REQUEST_START/END, queue, prefill, decode, speculation proposed/accepted/rejected,
GPU/CPU execution, KV/tensor allocate/release/reuse, model/adapter residency,
kernel/graph hit/miss, transfer H2D/D2H, cache read/write, reserve/release,
retry, failure, cancellation, recompute, reuse-avoided, energy and
cost-adjustment.

## Request accounting

A canonical RequestAccount exposes wall latency, queue time, execution, prefill
and decode time, GPU/CPU active time, transfer bytes by direction, KV bytes
allocated/peak/reused, tensor bytes allocated/peak/reused, model/adapter
residency byte-seconds, reservations acquired, allocation peak, speculative
proposed/accepted/rejected tokens, generated tokens, retry count, failed/cancelled
work, recomputed work, reuse-avoided work, energy and cost (where a policy is
supplied). Attempt totals reconcile to the request total.

## Shared-cost attribution

Explicit, persisted policies: equal share, proportional bytes/tokens/execution/
reserved, direct ownership, policy-defined weighted share. The residual is
reported as explicitly-unallocated overhead so component shares plus overhead
equal the source total with no hidden rounding drift.

## Retries and waste

Each attempt is independently attributable. Failed-attempt work, cancelled work,
rejected speculation and recomputation are reported separately from accepted
output. A stale attempt completion never adds cost to the current request.

## Reuse economics

KV hits, prefix reuse, tensor reuse, model/adapter reuse, kernel/graph hits and
warm residency are accounted. Counterfactual avoidance is preserved as
derived/estimated, never measured. Exposure: work consumed, work avoided, gross
resource use, net useful work, waste ratio and reuse credit.

## Pricing policies

Typed, versioned, immutable pricing policies with explicit rates (GPU/CPU
$/second, memory/GiB-second, transfer/GiB, storage, persistent cache, energy
$/kWh, custom device rates). Changing rates creates a new policy generation.
Historical physical records never mutate; cost can be recomputed under any
policy without rewriting consumption.

## Persistence and replay

Strict versioned persistence with deterministic binary encoding and checksum
protection. Malformed lengths, truncation, corruption, duplicate ids, invalid
enums/units/generations, NaN/Inf, negative physical quantities and trailing
garbage are rejected. Supports append, snapshot, save, recovery, indexed query
and replay. Recovery never resurrects stale authority; replay reproduces
identical physical totals and a stable digest.

## Distributed authority

A real coordinator and worker/accounting-source path over framed TCP supports
registration, WorkerBootId, coordinator epoch, event submission, acknowledgements,
duplicate rejection, stale-authority rejection, source restart, generation roll,
worker loss and fresh authority adoption. Socket ownership is safe, per-connection
writes are serialized, and no network I/O happens under the ledger/state lock.

## CUDA proof

A real CUDA accounting proof on an sm_120 (RTX 5090) device performs real
cudaMalloc, H2D/D2H, prefill-like and decode-like kernels, emits ledger events
from measured timings, and proves allocations return to physical baseline. It
exercises cold, warm/reuse and failed-retry paths.

## Benchmarks

Actual measurements (see BENCHMARKS.md) for append throughput, indexed lookup,
request-account reconstruction, aggregate, batch reconcile, binary
serialization, persistence save/reload, pricing recomputation, concurrent
ingestion and replay.

## Build / install / use

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    cmake --install build --prefix <prefix>
    ctest --test-dir build

Downstream:

    find_package(InferenceLedger REQUIRED)
    target_link_libraries(app PRIVATE InferenceLedger::inference_ledger)

The `iledger` CLI exposes list (text or CSV), inspect, request (text or
JSON), explain, attempt, tenant, model, batch, device, resources, cost,
waste, reuse, compare, pricing, snapshot, save, recover, replay, serve,
worker, multiprocess, cuda and benchmark.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
