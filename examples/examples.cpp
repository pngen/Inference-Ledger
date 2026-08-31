// examples.cpp
// Runnable examples covering the meaningful accounting boundaries.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "inference-ledger/batch.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/persistence.hpp"
#include "inference-ledger/pricing.hpp"
#include "inference-ledger/query.hpp"
#include "inference-ledger/request_account.hpp"

using namespace iledger;

namespace {
const LedgerId LID{0x494C4544474552ULL, 1};
const TenantId TEN{0x99, 1};
const WorkerId W{0xAA, 1};
const WorkerBootId BOOT{0xB007, 1};
const CoordinatorEpoch EP(1);
const AccountingGeneration AG(1);
const RequestGeneration RG(1);
const AttemptGeneration ATG(1);
const DispatchId DISP{0x3001, 1};

LedgerEntry ev(const LedgerEntryId& id, const RequestId& req, const AttemptId& at,
               EventKind k, ResourceKind rk, double v, Unit u, bool has_end,
               std::uint64_t s, std::uint64_t e, const std::string& batch = "") {
  LedgerEntry en;
  en.id = id; en.ledger = LID; en.tenant = TEN; en.workload = WorkloadId{0x88,1};
  en.request = req; en.model = ModelId{0x77,1}; en.model_revision = ModelRevisionId{0x77,2};
  en.attempt = at; en.dispatch = DISP; en.worker = W; en.node = NodeId{0x6002,1};
  en.device = DeviceId{0x6001,1}; en.event_kind = k; en.resource_kind = rk;
  en.quantity.value = v; en.quantity.unit = u; en.quantity.provenance = Provenance::Measured;
  en.start_ts_ns = s; en.end_ts_ns = e; en.has_end = has_end;
  en.source.worker = W; en.source.boot = BOOT; en.source.accounting_generation = AG;
  en.authority.epoch = EP; en.authority.worker_boot = BOOT;
  en.authority.request_generation = RG; en.authority.attempt = at;
  en.authority.attempt_generation = ATG; en.authority.accounting_generation = AG;
  en.authority.dispatch = DISP;
  if (!batch.empty()) en.metadata["batch"] = batch;
  return en;
}

void section(const std::string& t) {
  std::cout << "\n==== " << t << " ====\n";
}
}  // namespace

int main() {
  section("1. Basic request accounting");
  {
    const RequestId R{0x1, 1};
    std::vector<LedgerEntry> es = {
      ev(LedgerEntryId{1,1}, R, AttemptId{0x2001,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
      ev(LedgerEntryId{2,1}, R, AttemptId{0x2001,1}, EventKind::Reserve, ResourceKind::Memory, 1, Unit::Count, false, 1100000000ULL, 0),
      ev(LedgerEntryId{3,1}, R, AttemptId{0x2001,1}, EventKind::KvAllocate, ResourceKind::Kv, 4096, Unit::Bytes, false, 1200000000ULL, 0),
      ev(LedgerEntryId{4,1}, R, AttemptId{0x2001,1}, EventKind::Prefill, ResourceKind::Compute, 0.5, Unit::Seconds, true, 1400000000ULL, 1900000000ULL),
      ev(LedgerEntryId{5,1}, R, AttemptId{0x2001,1}, EventKind::Decode, ResourceKind::Compute, 64, Unit::Count, true, 2000000000ULL, 2400000000ULL),
      ev(LedgerEntryId{6,1}, R, AttemptId{0x2001,1}, EventKind::TransferH2D, ResourceKind::Transfer, 1024, Unit::Bytes, true, 1300000000ULL, 1350000000ULL),
      ev(LedgerEntryId{7,1}, R, AttemptId{0x2001,1}, EventKind::ModelResidency, ResourceKind::Residency, 1048576, Unit::Bytes, true, 1400000000ULL, 2400000000ULL),
      ev(LedgerEntryId{8,1}, R, AttemptId{0x2001,1}, EventKind::Release, ResourceKind::Memory, 1, Unit::Count, false, 2600000000ULL, 0),
      ev(LedgerEntryId{9,1}, R, AttemptId{0x2001,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 2700000000ULL, 0),
    };
    RequestAccount acc = reconcile_request(es, R);
    std::cout << "completed=" << acc.completed << " wall=" << acc.wall_latency_s.value
              << "s prefill=" << acc.prefill_s.value << "s decode=" << acc.decode_s.value
              << "s tokens=" << acc.generated_tokens << " kv=" << acc.kv_allocated.value
              << "B model_byte_seconds=" << acc.model_byte_seconds.value << "\n";
  }

  section("2. Batch cost attribution");
  {
    const std::string b = BatchId{0xB, 1}.to_string();
    const RequestId R1{0x11,1}, R2{0x12,1}, R3{0x13,1};
    std::vector<LedgerEntry> es;
    auto add = [&](const RequestId& r, std::uint64_t tok, double gpu_s, int seq) {
      es.push_back(ev(LedgerEntryId{(std::uint64_t)seq,1}, r, AttemptId{0x2001,1}, EventKind::RequestStart,
                      ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0, b));
      es.push_back(ev(LedgerEntryId{(std::uint64_t)(seq+1),1}, r, AttemptId{0x2001,1}, EventKind::Decode,
                      ResourceKind::Compute, (double)tok, Unit::Count, true, 1400000000ULL, 1500000000ULL, b));
      es.push_back(ev(LedgerEntryId{(std::uint64_t)(seq+2),1}, r, AttemptId{0x2001,1}, EventKind::GpuExecution,
                      ResourceKind::Compute, gpu_s, Unit::Seconds, true, 1400000000ULL, 1600000000ULL, b));
      es.push_back(ev(LedgerEntryId{(std::uint64_t)(seq+3),1}, r, AttemptId{0x2001,1}, EventKind::RequestEnd,
                      ResourceKind::Generic, 0, Unit::Count, false, 1700000000ULL, 0, b));
    };
    add(R1, 10, 0.1, 1); add(R2, 20, 0.2, 11); add(R3, 30, 0.3, 21);
    BatchAccount ba = reconcile_batch(es, BatchId{0xB,1});
    std::vector<RequestAccount> members = ba.member_accounts;
    // Shared batch GPU cost attributed proportional to tokens.
    const double totalGpu = ba.total_gpu_s;
    const double rate = 2.0;
    SharedAllocation alloc = allocate_shared(totalGpu * rate, ba.members, member_weights_for_policy(members, AttributionPolicy::ProportionalTokens),
                                             AttributionPolicy::ProportionalTokens);
    std::cout << "batch members=" << members.size() << " total_gpu_s=" << totalGpu << "\n";
    for (std::size_t i = 0; i < alloc.members.size(); ++i)
      std::cout << "  member " << alloc.members[i].request.to_string().substr(0,8)
                << " token_share=" << alloc.members[i].weight
                << " cost=" << alloc.members[i].share << "\n";
    std::cout << "  unallocated_overhead=" << alloc.unallocated_overhead
              << " reconciles=" << alloc.reconciles() << "\n";
  }

  section("3. Residency + reuse economics (cold vs warm)");
  {
    const RequestId RCold{0x21,1}, RWarm{0x22,1};
    std::vector<LedgerEntry> es = {
      ev(LedgerEntryId{1,1}, RCold, AttemptId{0x2001,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
      ev(LedgerEntryId{2,1}, RCold, AttemptId{0x2001,1}, EventKind::ModelResidency, ResourceKind::Residency, 2097152, Unit::Bytes, true, 1400000000ULL, 2400000000ULL),
      ev(LedgerEntryId{3,1}, RCold, AttemptId{0x2001,1}, EventKind::Prefill, ResourceKind::Compute, 0.5, Unit::Seconds, true, 1400000000ULL, 1900000000ULL),
      ev(LedgerEntryId{4,1}, RCold, AttemptId{0x2001,1}, EventKind::KvAllocate, ResourceKind::Kv, 8192, Unit::Bytes, false, 1200000000ULL, 0),
      ev(LedgerEntryId{5,1}, RCold, AttemptId{0x2001,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 2500000000ULL, 0),
      // warm reuses KV, no prefill, avoid recomputation
      ev(LedgerEntryId{6,1}, RWarm, AttemptId{0x2001,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
      ev(LedgerEntryId{7,1}, RWarm, AttemptId{0x2001,1}, EventKind::KvReuse, ResourceKind::Kv, 8192, Unit::Bytes, false, 1100000000ULL, 0),
      ev(LedgerEntryId{8,1}, RWarm, AttemptId{0x2001,1}, EventKind::ReuseAvoided, ResourceKind::Compute, 0.5, Unit::Seconds, false, 1200000000ULL, 0),
      ev(LedgerEntryId{9,1}, RWarm, AttemptId{0x2001,1}, EventKind::KernelHit, ResourceKind::Cache, 1, Unit::Count, false, 1300000000ULL, 0),
      ev(LedgerEntryId{10,1}, RWarm, AttemptId{0x2001,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 1400000000ULL, 0),
    };
    RequestAccount cold = reconcile_request(query_ledger(es, {.request = RCold}), RCold);
    RequestAccount warm = reconcile_request(query_ledger(es, {.request = RWarm}), RWarm);
    std::cout << "cold:  prefill_s=" << cold.prefill_s.value << " model_byte_seconds=" << cold.model_byte_seconds.value << "\n";
    std::cout << "warm:  prefill_s=" << warm.prefill_s.value << " kv_reuse=" << warm.kv_reuse.value
              << " reuse_credit=" << warm.reuse_credit() << " reuse_rate=" << warm.reuse_rate() << "\n";
    std::cout << "cold vs warm: prefill " << cold.prefill_s.value << " -> " << warm.prefill_s.value
              << "; reuse_credit " << warm.reuse_credit() << "\n";
  }

  section("4. Speculation + retry/waste accounting");
  {
    const RequestId R{0x31,1};
    const AttemptId A1{0x2001,1}, A2{0x2002,1};
    const AttemptGeneration G1(1), G2(2);
    auto mk = [&](int seq, const AttemptId& at, const AttemptGeneration& g, EventKind k, double v, Unit u) {
      (void)g;
      return ev(LedgerEntryId{(std::uint64_t)seq,1}, R, at, k, ResourceKind::Compute, v, u, true, 1000000000ULL, 1000001000ULL);
    };
    std::vector<LedgerEntry> es = {
      ev(LedgerEntryId{1,1}, R, A1, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
      mk(2, A1, G1, EventKind::SpeculationProposed, 16, Unit::Count),
      mk(3, A1, G1, EventKind::SpeculationAccepted, 10, Unit::Count),
      mk(4, A1, G1, EventKind::SpeculationRejected, 6, Unit::Count),
      // attempt 1 fails
      ev(LedgerEntryId{5,1}, R, A1, EventKind::Failure, ResourceKind::Generic, 0, Unit::Count, false, 1300000000ULL, 0),
      ev(LedgerEntryId{6,1}, R, A1, EventKind::Retry, ResourceKind::Generic, 1, Unit::Count, false, 1400000000ULL, 0),
      // attempt 2 succeeds
      ev(LedgerEntryId{7,1}, R, A2, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1500000000ULL, 0),
      mk(8, A2, G2, EventKind::Decode, 32, Unit::Count),
      ev(LedgerEntryId{9,1}, R, A2, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 1700000000ULL, 0),
    };
    RequestAccount acc = reconcile_request(es, R);
    std::cout << "completed=" << acc.completed << " retries=" << acc.retries
              << " attempts=" << acc.attempt_count << "\n";
    std::cout << "spec proposed/accepted/rejected = " << acc.spec_proposed << "/"
              << acc.spec_accepted << "/" << acc.spec_rejected << "\n";
    std::cout << "generated_tokens=" << acc.generated_tokens
              << " (rejected " << acc.spec_rejected << " NOT counted as output)\n";
    std::cout << "failed_work_s=" << acc.failed_attempt_work.value
              << " waste_ratio=" << acc.waste_ratio() << "\n";
  }

  section("5. Pricing-policy recalculation");
  {
    const RequestId R{0x41,1};
    std::vector<LedgerEntry> es = {
      ev(LedgerEntryId{1,1}, R, AttemptId{0x2001,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
      ev(LedgerEntryId{2,1}, R, AttemptId{0x2001,1}, EventKind::GpuExecution, ResourceKind::Compute, 1.0, Unit::Seconds, true, 1400000000ULL, 2400000000ULL),
      ev(LedgerEntryId{3,1}, R, AttemptId{0x2001,1}, EventKind::TransferH2D, ResourceKind::Transfer, 536870912, Unit::Bytes, false, 1300000000ULL, 0),
      ev(LedgerEntryId{4,1}, R, AttemptId{0x2001,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 2500000000ULL, 0),
    };
    RequestAccount acc = reconcile_request(es, R);
    const double gpu_before = acc.gpu_active_s.value;
    PricingPolicy cheap;  cheap.id = PricingPolicyId{1,1}; cheap.generation = 1; cheap.currency = "usd";
    cheap.rates.gpu_per_second = 1.0; cheap.rates.transfer_per_gib = 0.05;
    PricingPolicy premium = cheap; premium.id = PricingPolicyId{2,2}; premium.generation = 2;
    premium.rates.gpu_per_second = 4.0; premium.rates.transfer_per_gib = 0.5;
    CostResult a = apply_pricing(acc, cheap);
    CostResult b = apply_pricing(acc, premium);
    std::cout << "cheap:   total=" << a.total() << " " << a.currency << "\n";
    std::cout << "premium: total=" << b.total() << " " << b.currency << "\n";
    std::cout << "physical gpu_s unchanged = " << gpu_before << " (pricing independent)\n";
    std::cout << "both reconcile = " << a.reconciles() << "/" << b.reconciles() << "\n";
  }

  section("6. Persistence / replay");
  {
    const std::string path = "example_ledger.db";
    const RequestId R{0x51,1};
    std::vector<LedgerEntry> es = {
      ev(LedgerEntryId{1,1}, R, AttemptId{0x2001,1}, EventKind::RequestStart, ResourceKind::Generic, 0, Unit::Count, false, 1000000000ULL, 0),
      ev(LedgerEntryId{2,1}, R, AttemptId{0x2001,1}, EventKind::Decode, ResourceKind::Compute, 8, Unit::Count, true, 1400000000ULL, 1600000000ULL),
      ev(LedgerEntryId{3,1}, R, AttemptId{0x2001,1}, EventKind::RequestEnd, ResourceKind::Generic, 0, Unit::Count, false, 1700000000ULL, 0),
    };
    std::string err;
    LedgerStore::save_snapshot(es, LID, path, err);
    LoadResult lr = LedgerStore::load(path);
    Ledger fresh(LID);
    LedgerStore::replay_into(fresh, path, err);
    std::cout << "saved/loaded " << lr.entries.size() << " entries; fingerprint "
              << lr.fingerprint.to_string() << "\n";
    std::cout << "replayed " << fresh.size() << " entries; fingerprint "
              << ledger_fingerprint(fresh.snapshot()).to_string()
              << " (stable=" << (lr.fingerprint == ledger_fingerprint(fresh.snapshot())) << ")\n";
    std::remove(path.c_str());
  }

  std::cout << "\nEXAMPLES_DONE\n";
  return 0;
}
