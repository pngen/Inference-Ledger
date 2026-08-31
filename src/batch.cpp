#include "inference-ledger/batch.hpp"

#include <algorithm>
#include <cstddef>
#include <map>

namespace iledger {

namespace {

std::vector<double> uniform_weights(std::size_t n) {
  return std::vector<double>(n, 1.0);
}

}  // namespace

SharedAllocation allocate_shared(double total,
                                 const std::vector<RequestId>& members,
                                 const std::vector<double>& weights,
                                 AttributionPolicy policy,
                                 RequestId direct_owner) {
  SharedAllocation out;
  out.policy = policy;
  out.total = total;
  const std::size_t n = members.size();

  if (n == 0) return out;

  if (policy == AttributionPolicy::DirectOwnership) {
    RequestId owner = direct_owner;
    if (owner.is_zero()) owner = members.front();
    for (const auto& m : members) {
      MemberShare ms;
      ms.request = m;
      ms.share = (m == owner) ? total : 0.0;
      ms.weight = (m == owner) ? 1.0 : 0.0;
      out.members.push_back(std::move(ms));
    }
    out.unallocated_overhead = 0.0;
    return out;
  }

  // Build effective weights.
  std::vector<double> w = weights;
  if (w.size() != n) w = uniform_weights(n);
  // For equal-share we force uniform.
  if (policy == AttributionPolicy::EqualShare) w = uniform_weights(n);

  double wsum = 0.0;
  for (double wi : w) wsum += wi;
  if (wsum <= 0.0) {
    w = uniform_weights(n);
    wsum = static_cast<double>(n);
  }

  double assigned = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    MemberShare ms;
    ms.request = members[i];
    ms.weight = w[i];
    ms.share = total * (w[i] / wsum);
    assigned += ms.share;
    out.members.push_back(std::move(ms));
  }

  // Exactly absorb the floating-point residual as unallocated overhead.
  out.unallocated_overhead = total - assigned;
  if (out.unallocated_overhead < 0.0 && out.unallocated_overhead > -1e-12) {
    out.unallocated_overhead = 0.0;
  }
  return out;
}

std::vector<double> member_weights_for_policy(
    const std::vector<RequestAccount>& members, AttributionPolicy policy) {
  std::vector<double> w;
  w.reserve(members.size());
  for (const auto& m : members) {
    double wi = 1.0;
    switch (policy) {
      case AttributionPolicy::ProportionalBytes:
        wi = m.kv_allocated.value + m.tensor_allocated.value;
        break;
      case AttributionPolicy::ProportionalTokens:
        wi = static_cast<double>(m.generated_tokens);
        break;
      case AttributionPolicy::ProportionalExecution:
        wi = m.execution_s.value;
        break;
      case AttributionPolicy::ProportionalReserved:
        wi = static_cast<double>(m.reservations_acquired);
        break;
      case AttributionPolicy::EqualShare:
      case AttributionPolicy::DirectOwnership:
      case AttributionPolicy::Weighted:
      default:
        wi = 1.0;
        break;
    }
    w.push_back(wi);
  }
  return w;
}

BatchAccount reconcile_batch(const std::vector<LedgerEntry>& entries,
                             const BatchId& batch) {
  BatchAccount ba;
  ba.batch = batch;

  std::map<RequestId, std::vector<LedgerEntry>> by_request;
  for (const auto& e : entries) {
    if (e.metadata.count("batch")) {
      // The batch id is recorded in the entry metadata; if it does not match
      // the requested batch, skip.
      if (e.metadata.at("batch") != batch.to_string()) continue;
    }
    by_request[e.request].push_back(e);
  }

  for (const auto& kvp : by_request) {
    ba.members.push_back(kvp.first);
    RequestAccount acc = reconcile_request(kvp.second, kvp.first);
    ba.member_accounts.push_back(acc);
    ba.total_execution_s += acc.execution_s.value;
    ba.total_generated_tokens += static_cast<double>(acc.generated_tokens);
    ba.total_gpu_s += acc.gpu_active_s.value;
    if (acc.cancelled) ++ba.cancelled_members;
    if (!acc.completed) ba.partial_completion = true;
  }
  if (ba.members.empty() && !entries.empty()) {
    ba.partial_completion = true;
  }
  return ba;
}

}  // namespace iledger
