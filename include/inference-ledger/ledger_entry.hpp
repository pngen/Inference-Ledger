// ledger_entry.hpp
// Immutable ledger record. Each entry is a single accounting fact with a stable
// identity, an authority envelope, a typed quantity and explicit provenance.
// Entries are append-only; a completed entry is never modified in place.
//
// The binary codec (codec.hpp) serialises entries sparsely: fields that are
// not applicable to an event kind are simply not written, so unsupported
// fields are never forced.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "inference-ledger/authority.hpp"
#include "inference-ledger/event_kind.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/resource.hpp"
#include "inference-ledger/unit.hpp"

namespace iledger {

// A timestamp in nanoseconds since the Unix epoch. Monotonic accounting
// derives duration from these; wall-clock rendering is purely informational.
using TimestampNs = std::uint64_t;

struct LedgerEntry {
  LedgerEntryId id{};
  LedgerId ledger{};
  TenantId tenant{};
  WorkloadId workload{};
  RequestId request{};
  ModelId model{};
  ModelRevisionId model_revision{};

  bool has_adapter = false;
  AdapterId adapter{};

  AttemptId attempt{};
  DispatchId dispatch{};

  WorkerId worker{};
  NodeId node{};
  DeviceId device{};

  EventKind event_kind = EventKind::RequestStart;
  ResourceKind resource_kind = ResourceKind::Generic;

  Quantity quantity{};

  // For a duration event, end_ts_ns >= start_ts_ns. For an instantaneous
  // event, has_end is false and the single timestamp is start_ts_ns.
  TimestampNs start_ts_ns = 0;
  TimestampNs end_ts_ns = 0;
  bool has_end = false;

  // Who produced this entry and under what authority.
  SourceIdentity source{};
  AuthorityEnvelope authority{};

  bool has_accounting_policy = false;
  AccountingPolicyId accounting_policy{};

  bool has_pricing_policy = false;
  PricingPolicyId pricing_policy{};

  std::map<std::string, std::string> metadata;

  bool operator==(const LedgerEntry& o) const noexcept {
    return id == o.id && ledger == o.ledger && tenant == o.tenant &&
           workload == o.workload && request == o.request && model == o.model &&
           model_revision == o.model_revision && has_adapter == o.has_adapter &&
           (!has_adapter || adapter == o.adapter) && attempt == o.attempt &&
           dispatch == o.dispatch && worker == o.worker && node == o.node &&
           device == o.device && event_kind == o.event_kind &&
           resource_kind == o.resource_kind && quantity.value == o.quantity.value &&
           quantity.unit == o.quantity.unit &&
           quantity.provenance == o.quantity.provenance &&
           start_ts_ns == o.start_ts_ns && end_ts_ns == o.end_ts_ns &&
           has_end == o.has_end && source.worker == o.source.worker &&
           source.boot == o.source.boot &&
           source.accounting_generation == o.source.accounting_generation &&
           authority.epoch == o.authority.epoch &&
           authority.worker_boot == o.authority.worker_boot &&
           authority.request_generation == o.authority.request_generation &&
           authority.accounting_generation == o.authority.accounting_generation &&
           authority.attempt == o.authority.attempt &&
           authority.attempt_generation == o.authority.attempt_generation &&
           authority.dispatch == o.authority.dispatch &&
           has_accounting_policy == o.has_accounting_policy &&
           (!has_accounting_policy || accounting_policy == o.accounting_policy) &&
           has_pricing_policy == o.has_pricing_policy &&
           (!has_pricing_policy || pricing_policy == o.pricing_policy) &&
           metadata == o.metadata;
  }

  bool operator!=(const LedgerEntry& o) const noexcept { return !(*this == o); }

  // Duration in nanoseconds (0 for instantaneous events).
  std::uint64_t duration_ns() const noexcept {
    return has_end ? (end_ts_ns >= start_ts_ns ? end_ts_ns - start_ts_ns : 0)
                   : 0;
  }
};

// Compute an entry's stable identity from its content fields when an explicit
// id is not supplied by the producer. This gives idempotent replay a canonical
// basis: the same event always maps to the same LedgerEntryId.
LedgerEntryId derive_entry_id(const LedgerEntry& e);

}  // namespace iledger
