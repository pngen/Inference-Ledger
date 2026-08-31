// authority_state.hpp
// Coordinator-held authoritative state used to fence accounting mutations.
//
// A stale completion (old worker after restart, old attempt or accounting
// generation) must never alter current totals. validate_authority() decides
// whether a single event is acceptable given the coordinator's view of the
// world. The ledger and the distributed coordinator both use this.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <map>

#include "inference-ledger/identity.hpp"
#include "inference-ledger/ledger_entry.hpp"

namespace iledger {

// On-disk authority snapshot for recovery. A recovered coordinator must
// reconstruct this exactly so it does not resurrect stale authority.
struct AuthorityState {
  CoordinatorEpoch epoch{};
  AccountingGeneration accounting_generation{};

  std::map<RequestId, RequestGeneration> request_generation;
  std::map<RequestId, AttemptId> current_attempt;
  std::map<RequestId, AttemptGeneration> attempt_generation;
  std::map<WorkerId, WorkerBootId> worker_boot;
  // Set true when a request's authority was rolled (e.g. a worker that owned
  // the request was lost). While rolled, any request-scoped event other than
  // a fresh RequestStart is rejected as stale until a new RequestStart adopts
  // a fresh authority.
  std::map<RequestId, bool> request_rolled;

  // Whether request-scoped generations are enforced (disabled for events
  // that never reference a request, e.g. coordinator lifecycle records).
  bool enforce_request_scope = true;
};

enum class AuthorityVerdict {
  Accept,       // current this event is valid and may be appended.
  StaleEpoch,   // event carries an old coordinator epoch.
  StaleWorker,  // event carries an old WorkerBootId for its worker.
  StaleGeneration,  // event carries an old request/attempt/accounting generation.
  UnknownWorker,
  Invalid   // malformed / incomplete authority (missing required field).
};

inline const char* authority_verdict_name(AuthorityVerdict v) noexcept {
  switch (v) {
    case AuthorityVerdict::Accept: return "accept";
    case AuthorityVerdict::StaleEpoch: return "stale_epoch";
    case AuthorityVerdict::StaleWorker: return "stale_worker";
    case AuthorityVerdict::StaleGeneration: return "stale_generation";
    case AuthorityVerdict::UnknownWorker: return "unknown_worker";
    case AuthorityVerdict::Invalid: return "invalid";
  }
  return "unknown";
}

// Validate a single event against the coordinator authority snapshot.
// The event's own authority envelope is compared field-by-field.
inline AuthorityVerdict validate_authority(const LedgerEntry& e,
                                           const AuthorityState& s) {
  if (e.authority.epoch != s.epoch) return AuthorityVerdict::StaleEpoch;
  if (e.authority.accounting_generation != s.accounting_generation)
    return AuthorityVerdict::StaleGeneration;

  // Worker boot is required whenever a worker is recorded.
  const auto boot_it = s.worker_boot.find(e.worker);
  if (boot_it == s.worker_boot.end()) return AuthorityVerdict::UnknownWorker;
  if (boot_it->second != e.authority.worker_boot)
    return AuthorityVerdict::StaleWorker;

  if (!s.enforce_request_scope) return AuthorityVerdict::Accept;

  // A rolled request rejects everything except a fresh RequestStart.
  const auto rolled = s.request_rolled.find(e.request);
  if (rolled != s.request_rolled.end() && rolled->second &&
      e.event_kind != EventKind::RequestStart) {
    return AuthorityVerdict::StaleGeneration;
  }

  // Request-scoped generations.
  const auto rg = s.request_generation.find(e.request);
  if (rg != s.request_generation.end() &&
      rg->second != e.authority.request_generation)
    return AuthorityVerdict::StaleGeneration;

  const auto at = s.current_attempt.find(e.request);
  if (at != s.current_attempt.end() &&
      at->second != e.authority.attempt)
    return AuthorityVerdict::StaleGeneration;

  const auto atg = s.attempt_generation.find(e.request);
  if (atg != s.attempt_generation.end() &&
      atg->second != e.authority.attempt_generation)
    return AuthorityVerdict::StaleGeneration;

  return AuthorityVerdict::Accept;
}

}  // namespace iledger
