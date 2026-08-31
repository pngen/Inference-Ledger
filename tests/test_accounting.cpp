// test_accounting.cpp
// Request accounting, batch reconciliation, shared cost, pricing, residency,
// reuse, speculation, retries and aggregates.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include "framework.hpp"
#include "inference-ledger/batch.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/persistence.hpp"
#include "inference-ledger/pricing.hpp"
#include "inference-ledger/query.hpp"
#include "inference-ledger/request_account.hpp"
#include "support.hpp"

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

LedgerEntry ev(const LedgerEntryId& id, EventKind k, ResourceKind rk, double v,
               Unit u, bool has_end, std::uint64_t s, std::uint64_t e,
               const RequestId& req = REQ, const AttemptId& at = ATT,
               const RequestGeneration& rg = RG, const AttemptGeneration& atg = ATG) {
  return make(id, req, at, k, rk, v, u, Provenance::Measured, has_end, s, e,
              LID, TEN, W, BOOT, EP, rg, atg, AG, DISP);
}
}  // namespace

TEST(accounting_reconcile_basic) {
  std::vector<LedgerEntry> es = {
    ev(LedgerEntryId{1,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
    ev(LedgerEntryId{2,1}, EventKind::Reserve, ResourceKind::Memory, 1, Unit::Count, false, 1100000000ULL, 0),
    ev(LedgerEntryId{3,1}, EventKind::KvAllocate, ResourceKind::Kv, 4096, Unit::Bytes, false, 1200000000ULL, 0),
    ev(LedgerEntryId{4,1}, EventKind::TransferH2D, ResourceKind::Transfer, 1024, Unit::Bytes, true, 1300000000ULL, 1350000000ULL),
    ev(LedgerEntryId{5,1}, EventKind::Prefill, ResourceKind::Compute, 0.5, Unit::Seconds, true, 1400000000ULL, 1900000000ULL),
    ev(LedgerEntryId{6,1}, EventKind::Decode, ResourceKind::Compute, 32, Unit::Count, true, 2000000000ULL, 2400000000ULL),
    ev(LedgerEntryId{7,1}, EventKind::ModelResidency, ResourceKind::Residency, 1048576, Unit::Bytes, true, 1400000000ULL, 2400000000ULL),
    ev(LedgerEntryId{8,1}, EventKind::TransferD2H, ResourceKind::Transfer, 256, Unit::Bytes, false, 2500000000ULL, 0),
    ev(LedgerEntryId{9,1}, EventKind::Release, ResourceKind::Memory, 1, Unit::Count, false, 2600000000ULL, 0),
    ev(LedgerEntryId{10,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 2700000000ULL, 0),
  };
  RequestAccount acc = reconcile_request(es, REQ);
  CHECK(acc.completed);
  CHECK_NEAR(acc.wall_latency_s.value, 1.7, 1e-9);
  CHECK_NEAR(acc.prefill_s.value, 0.5, 1e-9);
  CHECK_NEAR(acc.decode_s.value, 0.4, 1e-9);
  CHECK_EQ(acc.generated_tokens, 32u);
  CHECK_NEAR(acc.kv_allocated.value, 4096.0, 1e-9);
  CHECK_NEAR(acc.kv_peak.value, 4096.0, 1e-9);
  CHECK_NEAR(acc.h2d_bytes.value, 1024.0, 1e-9);
  CHECK_NEAR(acc.d2h_bytes.value, 256.0, 1e-9);
  CHECK_EQ(acc.reservations_acquired, 1u);
  CHECK_EQ(acc.reservations_released, 1u);
  CHECK_NEAR(acc.model_byte_seconds.value, 1048576.0, 1e-3);  // 1s * 1MiB
  CHECK(acc.reconcile.ok);
}

TEST(accounting_reconcile_retry) {
  const AttemptId ATT2{0x2002, 1};
  const AttemptGeneration ATG2(2);
  std::vector<LedgerEntry> es = {
    ev(LedgerEntryId{1,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0, REQ, ATT, RG, ATG),
    ev(LedgerEntryId{2,1}, EventKind::Prefill, ResourceKind::Compute, 0.1, Unit::Seconds, true, 1100000000ULL, 1200000000ULL, REQ, ATT, RG, ATG),
    ev(LedgerEntryId{3,1}, EventKind::Failure, ResourceKind::Generic, 0, Unit::Count, false, 1300000000ULL, 0, REQ, ATT, RG, ATG),
    ev(LedgerEntryId{4,1}, EventKind::Retry, ResourceKind::Generic, 1, Unit::Count, false, 1400000000ULL, 0, REQ, ATT, RG, ATG),
    ev(LedgerEntryId{5,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1500000000ULL, 0, REQ, ATT2, RG, ATG2),
    ev(LedgerEntryId{6,1}, EventKind::Prefill, ResourceKind::Compute, 0.1, Unit::Seconds, true, 1600000000ULL, 1700000000ULL, REQ, ATT2, RG, ATG2),
    ev(LedgerEntryId{7,1}, EventKind::Decode, ResourceKind::Compute, 16, Unit::Count, true, 1800000000ULL, 1900000000ULL, REQ, ATT2, RG, ATG2),
    ev(LedgerEntryId{8,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 2000000000ULL, 0, REQ, ATT2, RG, ATG2),
  };
  RequestAccount acc = reconcile_request(es, REQ);
  CHECK(acc.completed);
  CHECK_EQ(acc.retries, 1u);
  CHECK_EQ(acc.attempt_count, 2u);
  CHECK_NEAR(acc.failed_attempt_work.value, 0.1, 1e-9);
  CHECK_NEAR(acc.execution_s.value, 0.2, 1e-9);
  CHECK_EQ(acc.generated_tokens, 16u);
}

TEST(accounting_reconcile_speculation) {
  std::vector<LedgerEntry> es = {
    ev(LedgerEntryId{1,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
    ev(LedgerEntryId{2,1}, EventKind::SpeculationProposed, ResourceKind::Compute, 12, Unit::Count, false, 1100000000ULL, 0),
    ev(LedgerEntryId{3,1}, EventKind::SpeculationAccepted, ResourceKind::Compute, 8, Unit::Count, false, 1200000000ULL, 0),
    ev(LedgerEntryId{4,1}, EventKind::SpeculationRejected, ResourceKind::Compute, 4, Unit::Count, true, 1300000000ULL, 1400000000ULL),
    ev(LedgerEntryId{5,1}, EventKind::Decode, ResourceKind::Compute, 24, Unit::Count, true, 1500000000ULL, 1700000000ULL),
    ev(LedgerEntryId{6,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 1800000000ULL, 0),
  };
  RequestAccount acc = reconcile_request(es, REQ);
  CHECK_EQ(acc.spec_proposed, 12u);
  CHECK_EQ(acc.spec_accepted, 8u);
  CHECK_EQ(acc.spec_rejected, 4u);
  CHECK_EQ(acc.generated_tokens, 32u);  // 8 accepted spec + 24 decode
  CHECK_NEAR(acc.spec_wasted_work_s.value, 0.1, 1e-9);  // 100 ms rejected spec
}

TEST(accounting_reconcile_reuse) {
  std::vector<LedgerEntry> es = {
    ev(LedgerEntryId{1,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
    ev(LedgerEntryId{2,1}, EventKind::KvReuse, ResourceKind::Kv, 8192, Unit::Bytes, false, 1100000000ULL, 0),
    ev(LedgerEntryId{3,1}, EventKind::ReuseAvoided, ResourceKind::Compute, 0.2, Unit::Seconds, false, 1200000000ULL, 0),
    ev(LedgerEntryId{4,1}, EventKind::Decode, ResourceKind::Compute, 8, Unit::Count, true, 1300000000ULL, 1400000000ULL),
    ev(LedgerEntryId{5,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 1500000000ULL, 0),
  };
  RequestAccount acc = reconcile_request(es, REQ);
  CHECK_NEAR(acc.kv_reuse.value, 8192.0, 1e-9);
  CHECK_NEAR(acc.reuse_avoided_work.value, 0.2, 1e-9);
  CHECK_NEAR(acc.reuse_credit(), 0.2, 1e-9);
}

TEST(batch_shared_equal_share) {
  std::vector<RequestId> members{RequestId{1,1}, RequestId{2,1}, RequestId{3,1}};
  SharedAllocation a = allocate_shared(100.0, members, {}, AttributionPolicy::EqualShare);
  CHECK(a.reconciles());
  for (const auto& m : a.members) CHECK_NEAR(m.share, 100.0 / 3.0, 1e-9);
}

TEST(batch_shared_proportional_tokens) {
  std::vector<RequestId> members{RequestId{1,1}, RequestId{2,1}, RequestId{3,1}};
  std::vector<RequestAccount> accounts(3);
  accounts[0].generated_tokens = 10; accounts[1].generated_tokens = 20; accounts[2].generated_tokens = 30;
  std::vector<double> w = member_weights_for_policy(accounts, AttributionPolicy::ProportionalTokens);
  SharedAllocation a = allocate_shared(60.0, members, w, AttributionPolicy::ProportionalTokens);
  CHECK(a.reconciles());
  CHECK_NEAR(a.members[0].share, 10.0, 1e-9);
  CHECK_NEAR(a.members[1].share, 20.0, 1e-9);
  CHECK_NEAR(a.members[2].share, 30.0, 1e-9);
}

TEST(batch_shared_proportional_bytes) {
  std::vector<RequestId> members{RequestId{1,1}, RequestId{2,1}};
  std::vector<RequestAccount> accounts(2);
  accounts[0].kv_allocated.value = 100; accounts[1].kv_allocated.value = 300;
  std::vector<double> w = member_weights_for_policy(accounts, AttributionPolicy::ProportionalBytes);
  SharedAllocation a = allocate_shared(400.0, members, w, AttributionPolicy::ProportionalBytes);
  CHECK_NEAR(a.members[0].share, 100.0, 1e-9);
  CHECK_NEAR(a.members[1].share, 300.0, 1e-9);
  CHECK(a.reconciles());
}

TEST(batch_shared_direct_ownership) {
  std::vector<RequestId> members{RequestId{1,1}, RequestId{2,1}};
  SharedAllocation a = allocate_shared(50.0, members, {}, AttributionPolicy::DirectOwnership, RequestId{2,1});
  CHECK_NEAR(a.members[0].share, 0.0, 1e-9);
  CHECK_NEAR(a.members[1].share, 50.0, 1e-9);
  CHECK(a.reconciles());
}

TEST(batch_reconcile_exact_no_drift) {
  for (int trial = 0; trial < 50; ++trial) {
    std::vector<RequestId> members;
    std::vector<double> w;
    for (int i = 0; i < 8; ++i) {
      members.push_back(RequestId{static_cast<std::uint64_t>(i), 1});
      w.push_back(static_cast<double>((i + 1) * 3));
    }
    const double total = 1234.5678;
    SharedAllocation a = allocate_shared(total, members, w, AttributionPolicy::Weighted);
    CHECK(a.reconciles());
  }
}

TEST(pricing_recompute_two_policies) {
  std::vector<LedgerEntry> es = {
    ev(LedgerEntryId{1,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
    ev(LedgerEntryId{2,1}, EventKind::Prefill, ResourceKind::Compute, 0.5, Unit::Seconds, true, 1400000000ULL, 1900000000ULL),
    ev(LedgerEntryId{3,1}, EventKind::Decode, ResourceKind::Compute, 32, Unit::Count, true, 2000000000ULL, 2400000000ULL),
    ev(LedgerEntryId{4,1}, EventKind::TransferH2D, ResourceKind::Transfer, 1024*1024, Unit::Bytes, false, 1300000000ULL, 0),
    ev(LedgerEntryId{5,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 2500000000ULL, 0),
  };
  RequestAccount acc = reconcile_request(es, REQ);
  const double gpu_before = acc.gpu_active_s.value;

  PricingPolicy p1;
  p1.id = PricingPolicyId{1,1}; p1.generation = 1; p1.currency = "usd"; p1.name = "p1";
  p1.rates.gpu_per_second = 2.0; p1.rates.transfer_per_gib = 0.5;
  PricingPolicy p2 = p1; p2.id = PricingPolicyId{2,2}; p2.generation = 2;
  p2.rates.gpu_per_second = 4.0; p2.rates.transfer_per_gib = 1.5;

  CostResult c1 = apply_pricing(acc, p1);
  CostResult c2 = apply_pricing(acc, p2);
  CHECK(c1.reconciles());
  CHECK(c2.reconciles());
  CHECK_NEAR(c2.compute_cost, c1.compute_cost * 2.0, 1e-9);
  CHECK_NEAR(c2.transfer_cost, c1.transfer_cost * 3.0, 1e-9);
  CHECK_NEAR(acc.gpu_active_s.value, gpu_before, 1e-12);
  CHECK_EQ(c1.currency, "usd");
}

TEST(pricing_deterministic) {
  std::vector<LedgerEntry> es = {
    ev(LedgerEntryId{1,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
    ev(LedgerEntryId{2,1}, EventKind::Decode, ResourceKind::Compute, 8, Unit::Count, true, 1400000000ULL, 1600000000ULL),
    ev(LedgerEntryId{3,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 1700000000ULL, 0),
  };
  RequestAccount acc = reconcile_request(es, REQ);
  PricingPolicy p;
  p.id = PricingPolicyId{1,1}; p.generation = 1; p.currency = "usd"; p.rates.gpu_per_second = 3.0;
  CostResult a = apply_pricing(acc, p);
  CostResult b = apply_pricing(acc, p);
  CHECK_NEAR(a.total(), b.total(), 1e-12);
  CHECK(a.reconciles() && b.reconciles());
  CHECK_NEAR(a.energy_kwh, 0.0, 1e-12);
}

TEST(accounting_aggregates) {
  std::vector<LedgerEntry> es1 = {
    ev(LedgerEntryId{1,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
    ev(LedgerEntryId{2,1}, EventKind::Decode, ResourceKind::Compute, 10, Unit::Count, true, 1400000000ULL, 1600000000ULL),
    ev(LedgerEntryId{3,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 1700000000ULL, 0),
  };
  std::vector<LedgerEntry> es2 = es1;
  for (auto& e : es2) e.request = RequestId{0x999,1};
  RequestAccount a1 = reconcile_request(es1, REQ);
  RequestAccount a2 = reconcile_request(es2, RequestId{0x999,1});
  std::vector<RequestAccount> all{a1, a2};
  Aggregate agg = aggregate_accounts(all);
  CHECK(agg.requests == 2u);
  CHECK_NEAR(agg.total_tokens, 20.0, 1e-9);
  CHECK_NEAR(agg.gpu_seconds_per_request, a1.gpu_active_s.value, 1e-9);
}
