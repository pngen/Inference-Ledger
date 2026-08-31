// authority.hpp
// Authority envelope for mutable accounting events.
//
// Every accounting mutation that can affect the authoritative ledger is fenced
// by a combination of identities and monotonic generations. A stale event
// (old worker after restart, old attempt generation, old accounting generation)
// is rejected before it can mutate current totals.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>

#include "inference-ledger/identity.hpp"

namespace iledger {

// The set of identifiers that fence an accounting event. A conforming
// coordinator validates every field against its current knowledge.
struct AuthorityEnvelope {
  CoordinatorEpoch epoch{};
  WorkerBootId worker_boot{};
  RequestGeneration request_generation{};
  AccountingGeneration accounting_generation{};
  AttemptId attempt{};
  AttemptGeneration attempt_generation{};
  DispatchId dispatch{};

  bool operator==(const AuthorityEnvelope& o) const noexcept {
    return epoch == o.epoch && worker_boot == o.worker_boot &&
           request_generation == o.request_generation &&
           accounting_generation == o.accounting_generation &&
           attempt == o.attempt && attempt_generation == o.attempt_generation &&
           dispatch == o.dispatch;
  }
};

// A generation is "fresh" relative to the coordinator only when it is strictly
// equal to the coordinator's current value for that axis. Anything older is
// stale; anything newer (future) is also rejected as out-of-order.
inline bool generation_is_fresh(CoordinatorEpoch observed, CoordinatorEpoch current) noexcept {
  return observed == current;
}
inline bool generation_is_fresh(AccountingGeneration observed, AccountingGeneration current) noexcept {
  return observed == current;
}

// Identity of the actor that produced an event. The combination of WorkerId,
// WorkerBootId and AccountingGeneration identifies a specific incarnation of
// an accounting source.
struct SourceIdentity {
  WorkerId worker;
  WorkerBootId boot;
  AccountingGeneration accounting_generation;
};

}  // namespace iledger
