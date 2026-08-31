// support.hpp
// Shared helpers for building ledger entries in tests.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once
#include <cstdint>
#include "inference-ledger/ledger_entry.hpp"

namespace iledger { namespace test {

inline LedgerEntry make(
    const LedgerEntryId& id, const RequestId& req, const AttemptId& attempt,
    EventKind kind, ResourceKind rk, double value, Unit unit,
    Provenance prov, bool has_end, std::uint64_t s, std::uint64_t e, const LedgerId& ledger,
    const TenantId& tenant, const WorkerId& worker, const WorkerBootId& boot,
    const CoordinatorEpoch& epoch, const RequestGeneration& rg,
    const AttemptGeneration& atg, const AccountingGeneration& ag,
    const DispatchId& disp) {
  LedgerEntry en;
  en.id = id;
  en.ledger = ledger;
  en.tenant = tenant;
  en.workload = WorkloadId{0x88, 0x1};
  en.request = req;
  en.model = ModelId{0x77, 0x1};
  en.model_revision = ModelRevisionId{0x77, 0x2};
  en.attempt = attempt;
  en.dispatch = disp;
  en.worker = worker;
  en.node = NodeId{0x6002, 0x1};
  en.device = DeviceId{0x6001, 0x1};
  en.event_kind = kind;
  en.resource_kind = rk;
  en.quantity.value = value;
  en.quantity.unit = unit;
  en.quantity.provenance = prov;
  en.start_ts_ns = s;
  en.end_ts_ns = e;
  en.has_end = has_end;
  en.source.worker = worker;
  en.source.boot = boot;
  en.source.accounting_generation = ag;
  en.authority.epoch = epoch;
  en.authority.worker_boot = boot;
  en.authority.request_generation = rg;
  en.authority.attempt = attempt;
  en.authority.attempt_generation = atg;
  en.authority.accounting_generation = ag;
  en.authority.dispatch = disp;
  return en;
}

}}  // namespace iledger::test
