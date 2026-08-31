# EXAMPLES

Runnable examples (build `iledger_examples`) and standalone proofs.

## `iledger_examples`

Covers the meaningful boundaries:

1. Basic request accounting - build a request and inspect the canonical account.
2. Batch cost attribution - three members share a batch GPU cost proportional to
   tokens; the allocation reconciles exactly.
3. Residency + reuse economics - cold vs warm request: warm reuses KV and avoids
   recomputation (reuse credit), with model residency byte-seconds reported.
4. Speculation + retry/waste - proposed/accepted/rejected speculation kept
   distinct; a failed attempt is retried under a fresh AttemptId; generated tokens
   only count accepted output.
5. Pricing-policy recalculation - the same physical ledger priced under two
   policies; physical consumption is unchanged.
6. Persistence / replay - save, load, replay; fingerprints are stable.

## Standalone proofs

- `iledger_multiprocess` - a real coordinator + two worker OS processes over
  framed TCP. Proves registration, request begin, reservation, dispatch, GPU /
  transfer accounting, worker A killed as an OS process, restart with a fresh
  WorkerBootId, authority roll, and that every stale epoch / boot / attempt /
  attempt-generation / accounting-generation / duplicate event is rejected over
  real TCP. Fresh post-restart accounting succeeds, the ledger is saved,
  reloaded and replayed to identical totals and a stable digest.
- `iledger_cuda_proof` - real CUDA on sm_120 (RTX 5090): real cudaMalloc, H2D,
  prefill-like and decode-like kernels, synchronization, D2H, CPU-reference
  verification and cudaFree, emitting ledger events from measured activity. Cold,
  warm/reuse and failed-retry paths are demonstrated; allocations return to
  baseline.

## CLI

    iledger list ledger.db --csv
    iledger explain ledger.db <request_id_hex>
    iledger request ledger.db <request_id_hex> --json
    iledger compare ledger.db <reqA_hex> <reqB_hex>
    iledger cost ledger.db <req_hex> --policy policy.cfg
    iledger batch ledger.db <batch_id_hex>
    iledger replay ledger.db
