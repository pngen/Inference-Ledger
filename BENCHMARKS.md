# BENCHMARKS

Actual measurements from `iledger_benchmark` (Windows / MSVC 19.44 Release, x64).
Synthetic dataset: 100,000 ledger entries across 2,000 requests (50 entries each).

| Operation                          | Measurement                    |
|------------------------------------|--------------------------------|
| Append throughput                  | ~2.3-3.1M entries/s (100k)     |
| Indexed lookup (100k ids)          | ~16-29 ms                      |
| Request-account reconstruction     | 100k entries in ~129-146 ms    |
| Aggregate (2,000 accounts)         | ~127-144 ms                    |
| Batch reconcile (200 members)      | ~9-20 ms                       |
| Binary encode (100k -> ~37-40 MiB) | ~296-317 ms                    |
| Binary encode+decode (100k)        | ~550-593 ms                    |
| Persistence save                   | ~331-352 ms                    |
| Persistence reload                 | ~1.8 s                         |
| Pricing recompute (2,000)          | ~0.07 ms                       |
| Replay (fresh append)              | ~32-40 ms                      |
| Concurrent ingestion (8 threads)   | ~65-91 ms for 100k             |

Timings are wall-clock and include the full operation; they report measured
performance on this machine and are not a cross-platform claim.

## How to run

    cmake --build build --config Release
    ./build/bin/iledger_benchmark.exe

Run against an existing ledger:

    ./build/bin/iledger_benchmark.exe ledger.db
