// consumer.cpp
// Downstream find_package(InferenceLedger) consumer.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <inference-ledger/ledger.hpp>
#include <inference-ledger/identity.hpp>
#include <inference-ledger/request_account.hpp>
#include <inference-ledger/pricing.hpp>
#include <inference-ledger/persistence.hpp>

using namespace iledger;

namespace {
LedgerEntry ev(const LedgerEntryId& id, const RequestId& req, const AttemptId& at,
               EventKind k, ResourceKind rk, double v, Unit u, bool has_end,
               std::uint64_t s, std::uint64_t e, const LedgerId& l, const TenantId& t,
               const WorkerId& w, const WorkerBootId& b, const CoordinatorEpoch& ep,
               const RequestGeneration& rg, const AttemptGeneration& atg,
               const AccountingGeneration& ag, const DispatchId& d) {
  LedgerEntry en;
  en.id = id; en.ledger = l; en.tenant = t; en.workload = WorkloadId{0x88,1};
  en.request = req; en.model = ModelId{0x77,1}; en.model_revision = ModelRevisionId{0x77,2};
  en.attempt = at; en.dispatch = d; en.worker = w; en.node = NodeId{0x6002,1};
  en.device = DeviceId{0x6001,1}; en.event_kind = k; en.resource_kind = rk;
  en.quantity.value = v; en.quantity.unit = u; en.quantity.provenance = Provenance::Measured;
  en.start_ts_ns = s; en.end_ts_ns = e; en.has_end = has_end;
  en.source.worker = w; en.source.boot = b; en.source.accounting_generation = ag;
  en.authority.epoch = ep; en.authority.worker_boot = b;
  en.authority.request_generation = rg; en.authority.attempt = at;
  en.authority.attempt_generation = atg; en.authority.accounting_generation = ag;
  en.authority.dispatch = d;
  return en;
}
}  // namespace

int main() {
  const LedgerId L{0x494C4544474552ULL, 1};
  Ledger ledger(L);
  const auto R = RequestId{0x1234, 1};
  ledger.append(ev(LedgerEntryId{1,1}, R, AttemptId{0x2001,1}, EventKind::RequestStart,
                   ResourceKind::Generic, 0, Unit::Count, false, 1'000'000'000ULL, 0,
                   L, TenantId{0x99,1}, WorkerId{0xAA,1}, WorkerBootId{0xB007,1},
                   CoordinatorEpoch(1), RequestGeneration(1), AttemptGeneration(1),
                   AccountingGeneration(1), DispatchId{0x3001,1}));
  ledger.append(ev(LedgerEntryId{2,1}, R, AttemptId{0x2001,1}, EventKind::Decode,
                   ResourceKind::Compute, 16, Unit::Count, true, 1'400'000'000ULL, 1'600'000'000ULL,
                   L, TenantId{0x99,1}, WorkerId{0xAA,1}, WorkerBootId{0xB007,1},
                   CoordinatorEpoch(1), RequestGeneration(1), AttemptGeneration(1),
                   AccountingGeneration(1), DispatchId{0x3001,1}));
  ledger.append(ev(LedgerEntryId{3,1}, R, AttemptId{0x2001,1}, EventKind::RequestEnd,
                   ResourceKind::Generic, 0, Unit::Count, false, 1'700'000'000ULL, 0,
                   L, TenantId{0x99,1}, WorkerId{0xAA,1}, WorkerBootId{0xB007,1},
                   CoordinatorEpoch(1), RequestGeneration(1), AttemptGeneration(1),
                   AccountingGeneration(1), DispatchId{0x3001,1}));

  RequestAccount acc = reconcile_request(ledger.snapshot(), R);
  if (!acc.completed || acc.generated_tokens != 16 || acc.attempt_count != 1) {
    std::cerr << "downstream consumer FAIL\n";
    return 1;
  }
  PricingPolicy p;
  p.id = PricingPolicyId{1,1}; p.generation = 1; p.currency = "usd";
  p.rates.gpu_per_second = 2.0;
  CostResult c = apply_pricing(acc, p);
  if (!c.reconciles()) { std::cerr << "downstream cost FAIL\n"; return 1; }

  // Persistence round-trip via the installed library.
  const std::string path = "consumer_ledger.db";
  std::string err;
  if (!LedgerStore::save_snapshot(ledger.snapshot(), L, path, err)) { std::cerr << "save FAIL\n"; return 1; }
  LoadResult rl = LedgerStore::load(path);
  std::remove(path.c_str());
  if (!rl.ok || rl.entries.size() != 3) { std::cerr << "reload FAIL\n"; return 1; }

  std::cout << "downstream consumer PASS\n";
  return 0;
}
