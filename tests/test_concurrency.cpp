// test_concurrency.cpp
// Concurrent ingestion, snapshot-during-mutation, same-request multi-source.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include "framework.hpp"
#include <algorithm>
#include <atomic>
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/query.hpp"
#include "inference-ledger/request_account.hpp"
#include "support.hpp"

#include <thread>
#include <vector>

using namespace iledger;
using namespace iledger::test;

namespace {
const LedgerId LID{0x494C4544474552ULL, 1};
const TenantId TEN{0x99, 1};
const WorkerId W{0xAA, 1};
const WorkerBootId BOOT{0xB007, 1};
const CoordinatorEpoch EP(1);
const AccountingGeneration AG(1);
const RequestId REQ{0x1234, 1};
const AttemptId ATT{0x2001, 1};
const RequestGeneration RG(1);
const AttemptGeneration ATG(1);
const DispatchId DISP{0x3001, 1};

LedgerEntry r_ev(const LedgerEntryId& id, EventKind k, ResourceKind rk, double v,
                 Unit u, bool has_end, std::uint64_t s, std::uint64_t e,
                 const RequestId& req = REQ) {
  return make(id, req, ATT, k, rk, v, u, Provenance::Measured, has_end, s, e,
              LID, TEN, W, BOOT, EP, RG, ATG, AG, DISP);
}
}  // namespace

TEST(concurrency_append_no_loss) {
  Ledger ledger(LID);
  const int kThreads = 8;
  const int kPerThread = 3000;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&ledger, kPerThread, t] {
      for (int i = 0; i < kPerThread; ++i) {
        const std::uint64_t id = static_cast<std::uint64_t>(t) * kPerThread + i;
        LedgerEntry e = r_ev(LedgerEntryId{id, 1}, EventKind::Decode,
                             ResourceKind::Compute, 8, Unit::Count, true,
                             1000 + id, 1200 + id,
                             RequestId{static_cast<std::uint64_t>(0x1000 + t), 1ULL});
        ledger.append(e);
      }
    });
  }
  for (auto& th : threads) th.join();
  CHECK_EQ(ledger.size(), static_cast<std::size_t>(kThreads * kPerThread));
}

TEST(concurrency_snapshot_during_mutation) {
  Ledger ledger(LID);
  std::atomic<bool> stop{false};
  const int kPerThread = 2000;
  // A writer thread that keeps appending to make a snapshot race impossible to
  // return a torn state.
  std::vector<std::thread> writers;
  for (int t = 0; t < 4; ++t) {
    writers.emplace_back([&ledger, &stop, t, kPerThread] {
      std::uint64_t id = static_cast<std::uint64_t>(t) * kPerThread;
      while (!stop.load()) {
        LedgerEntry e = r_ev(LedgerEntryId{id++, 1}, EventKind::Decode,
                             ResourceKind::Compute, 8, Unit::Count, true,
                             1000, 1200,
                             RequestId{static_cast<std::uint64_t>(0x2000 + t), 1ULL});
        ledger.append(e);
      }
    });
  }
  // Snapshot repeatedly while writers run.
  std::size_t last = ledger.size();
  for (int i = 0; i < 200; ++i) {
    const auto snap = ledger.snapshot();
    CHECK(snap.size() >= last || snap.size() == 0);  // monotonic growth, never shrinks
    last = std::max(last, snap.size());
  }
  stop.store(true);
  for (auto& w : writers) w.join();
}

TEST(concurrency_same_request_multisource) {
  Ledger ledger(LID);
  // Two source threads submit distinct events for the SAME request with the
  // same authority envelope; every event is accepted and totals reconcile.
  std::thread a([&ledger] {
    for (int i = 0; i < 200; ++i) {
      ledger.append(r_ev(LedgerEntryId{static_cast<std::uint64_t>(1000 + i), 1},
                         EventKind::Decode, ResourceKind::Compute, 8, Unit::Count,
                         true, 1000 + i * 10, 1200 + i * 10, RequestId{0x9999, 1}));
    }
  });
  std::thread b([&ledger] {
    for (int i = 0; i < 200; ++i) {
      ledger.append(r_ev(LedgerEntryId{static_cast<std::uint64_t>(2000 + i), 1},
                         EventKind::Prefill, ResourceKind::Compute, 0.1, Unit::Seconds,
                         true, 1000 + i * 10, 1200 + i * 10, RequestId{0x9999, 1}));
    }
  });
  a.join(); b.join();
  CHECK_EQ(ledger.size(), 400u);
  // No request start/end -> reconcile reports it as not completed but all
  // entries counted without error.
  RequestAccount acc = reconcile_request(query_ledger(ledger.snapshot(), {.request = RequestId{0x9999, 1}}), RequestId{0x9999,1});
  CHECK(acc.reconcile.ok);
  CHECK(acc.attempt_count >= 1);
}
