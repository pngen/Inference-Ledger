// test_property.cpp
// Deterministic property-based invariants over randomized ledger streams.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include "framework.hpp"
#include "inference-ledger/batch.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/persistence.hpp"
#include "inference-ledger/query.hpp"
#include "inference-ledger/request_account.hpp"
#include "support.hpp"

#include <cmath>
#include <random>

using namespace iledger;
using namespace iledger::test;

namespace {
const LedgerId LID{0x494C4544474552ULL, 1};
const TenantId TEN{0x99, 1};
const WorkerId W{0xAA, 1};
const WorkerBootId BOOT{0xB007, 1};
const CoordinatorEpoch EP(1);
const AccountingGeneration AG(1);
const AttemptId ATT{0x2001, 1};
const RequestGeneration RG(1);
const AttemptGeneration ATG(1);
const DispatchId DISP{0x3001, 1};

LedgerEntry rnd_event(std::mt19937_64& rng, const LedgerId& lid, std::uint64_t seq) {
  std::uniform_int_distribution<std::uint64_t> didist(1, 6);
  RequestId req{didist(rng), 1};
  AttemptId att{didist(rng), 1};
  std::uniform_int_distribution<int> kind(5, 12);
  EventKind k = static_cast<EventKind>(kind(rng));
  std::uniform_int_distribution<int> rk(1, 10);
  ResourceKind res = static_cast<ResourceKind>(rk(rng));
  std::uniform_int_distribution<int> unit(0, 3);
  Unit u = static_cast<Unit>(unit(rng));
  std::uniform_real_distribution<double> val(0.0, 1e6);
  double v = (u == Unit::Count) ? static_cast<double>(static_cast<std::uint64_t>(val(rng))) : val(rng);
  return make(LedgerEntryId{seq, 1}, req, att, k, res, v, u,
              Provenance::Measured, true, 1000 + seq * 10, 1000 + seq * 10 + 50,
              lid, TEN, W, BOOT, EP, RequestGeneration(1), AttemptGeneration(1), AG,
              DispatchId{1, 1});
}
}  // namespace

TEST(property_no_negative_no_nan) {
  std::mt19937_64 rng(0xC0FFEE);
  for (int trial = 0; trial < 20; ++trial) {
    std::vector<LedgerEntry> es;
    for (int i = 0; i < 200; ++i) es.push_back(rnd_event(rng, LID, static_cast<std::uint64_t>(i)));
    Ledger ledger(LID);
    for (const auto& e : es) ledger.append(e);
    // Verify no NaN/Inf or negative physical in every stored entry.
    for (const auto& e : ledger.snapshot()) {
      CHECK(e.quantity.is_finite());
      if (e.resource_kind != ResourceKind::Cost) CHECK(e.quantity.is_non_negative());
    }
    // Reconcile every distinct request; invariants must hold.
    for (const auto& req : distinct_requests(ledger.snapshot())) {
      RequestAccount acc = reconcile_request(query_ledger(ledger.snapshot(), {.request = req}), req);
      CHECK(acc.wall_latency_s.is_finite());
      CHECK(acc.execution_s.is_finite());
      CHECK(acc.gpu_active_s.is_non_negative());
      CHECK(std::isfinite(acc.waste_ratio()));
      CHECK(acc.reuse_credit() >= 0.0);
    }
  }
}

TEST(property_idempotent_replay_no_dup) {
  std::mt19937_64 rng(0xDEADBEEF);
  std::vector<LedgerEntry> es;
  for (int i = 0; i < 500; ++i) es.push_back(rnd_event(rng, LID, static_cast<std::uint64_t>(i)));
  // Append twice: idempotency must hold, count stays 500, digest stable.
  Ledger ledger(LID);
  for (const auto& e : es) ledger.append(e);
  const std::size_t n1 = ledger.size();
  for (const auto& e : es) ledger.append(e);
  CHECK_EQ(ledger.size(), n1);
  const auto snap = ledger.snapshot();
  CHECK_EQ(snap.size(), n1);
  const auto fp = ledger_fingerprint(snap);
  // Deterministic: same stream -> same fingerprint.
  Ledger ledger2(LID);
  for (const auto& e : es) ledger2.append(e);
  CHECK(ledger_fingerprint(ledger2.snapshot()) == fp);
}

TEST(property_shared_alloc_always_reconciles) {
  std::mt19937_64 rng(7);
  for (int trial = 0; trial < 200; ++trial) {
    std::vector<RequestId> members;
    std::vector<double> w;
    const int n = 1 + static_cast<int>(rng() % 12);
    for (int i = 0; i < n; ++i) {
      members.push_back(RequestId{rng() % 1000, 1});
      w.push_back(static_cast<double>(rng() % 1000 + 1));
    }
    const double total = static_cast<double>(rng() % 1000000) + 0.5;
    for (AttributionPolicy p : {AttributionPolicy::EqualShare, AttributionPolicy::Weighted,
                                AttributionPolicy::ProportionalTokens}) {
      SharedAllocation a = allocate_shared(total, members, w, p);
      CHECK(a.reconciles());
    }
  }
}

TEST(property_no_double_count_reservations) {
  std::mt19937_64 rng(99);
  for (int trial = 0; trial < 50; ++trial) {
    Ledger ledger(LID);
    // Exactly one request with a balanced hold/release sequence.
    const RequestId REQB{0xABCD, 1};
    const std::uint64_t base = 1000000000ULL;
    for (int i = 0; i < 10; ++i) {
      LedgerEntry r = make(LedgerEntryId{static_cast<std::uint64_t>(2*i+1), 1}, REQB, ATT, EventKind::Reserve,
                           ResourceKind::Memory, 1, Unit::Count, Provenance::Measured, false, base, 0,
                           LID, TEN, W, BOOT, EP, RG, ATG, AG, DISP);
      LedgerEntry rel = make(LedgerEntryId{static_cast<std::uint64_t>(2*i+2), 1}, REQB, ATT, EventKind::Release,
                             ResourceKind::Memory, 1, Unit::Count, Provenance::Measured, false, base + 1, 0,
                             LID, TEN, W, BOOT, EP, RG, ATG, AG, DISP);
      ledger.append(r);
      ledger.append(rel);
    }
    RequestAccount acc = reconcile_request(query_ledger(ledger.snapshot(), {.request = REQB}), REQB);
    CHECK_EQ(acc.reservations_acquired, 10u);
    CHECK_EQ(acc.reservations_released, 10u);
    CHECK(acc.reservations_released <= acc.reservations_acquired);
  }
}
