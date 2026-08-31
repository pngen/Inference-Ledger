#include "inference-ledger/pricing.hpp"

#include <cmath>

namespace iledger {

namespace {
constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
constexpr double kJoulesPerKwh = 3.6e6;
}

bool PricingRates::all_finite() const {
  const double v[] = {cpu_per_second,
                      gpu_per_second,
                      memory_per_gib_second,
                      host_memory_per_gib_second,
                      pinned_memory_per_gib_second,
                      transfer_per_gib,
                      storage_per_gib_second,
                      persistent_cache_cost,
                      energy_per_kwh};
  for (double d : v) {
    if (!std::isfinite(d)) return false;
  }
  for (const auto& kv : custom_device_per_second) {
    if (!std::isfinite(kv.second)) return false;
  }
  return true;
}

bool CostResult::reconciles() const {
  const double sum = useful_work_cost + wasted_work_cost + cancelled_cost +
                     recomputed_cost + cpu_compute_cost + residency_cost +
                     transfer_cost + storage_cache_cost + energy_cost;
  const double t = total();
  return std::fabs(sum - t) < 1e-9;
}

double CostResult::useful_fraction() const {
  const double t = total();
  return t > 0.0 ? useful_work_cost / t : 0.0;
}

CostResult apply_pricing(const RequestAccount& acc,
                         const PricingPolicy& policy) {
  CostResult r;
  r.policy_id = policy.id;
  r.policy_generation = policy.generation;
  r.currency = policy.currency;
  const auto& rates = policy.rates;

  // Compute costs from physical seconds.
  const double gpu_rate = rates.gpu_per_second;
  const double cpu_rate = rates.cpu_per_second;

  // Split by category.
  r.useful_work_cost = acc.execution_s.value * gpu_rate;
  r.wasted_work_cost =
      (acc.failed_attempt_work.value + acc.spec_wasted_work_s.value) * gpu_rate;
  r.cancelled_cost = acc.cancelled_work.value * gpu_rate;
  r.retry_cost = acc.failed_attempt_work.value * gpu_rate;
  r.rejected_spec_cost = acc.spec_wasted_work_s.value * gpu_rate;
  r.recomputed_cost = acc.recomputed_work.value * gpu_rate;

  // CPU contributes to compute cost but is reported here as part of total
  // compute; we fold it into useful work for the useful bucket only when the
  // request completed. For simplicity, CPU time is billed at its own rate and
  // added to compute_cost, but the useful split uses GPU time (the dominant
  // cost).
  r.cpu_compute_cost = acc.cpu_active_s.value * cpu_rate;
  r.compute_cost = r.useful_work_cost + r.wasted_work_cost + r.cancelled_cost +
                   r.recomputed_cost + r.cpu_compute_cost;

  // Residency.
  r.residency_cost = (acc.model_byte_seconds.value + acc.adapter_byte_seconds.value) /
                     kGiB * rates.memory_per_gib_second;

  // Transfers.
  const double transfer_bytes = acc.h2d_bytes.value + acc.d2h_bytes.value +
                                acc.inter_node_bytes.value;
  r.transfer_cost = transfer_bytes / kGiB * rates.transfer_per_gib;

  // Storage / persistent cache.
  r.storage_cache_cost = static_cast<double>(acc.cache_writes) *
                         rates.persistent_cache_cost;

  // Energy.
  r.energy_kwh = acc.energy_joules.value / kJoulesPerKwh;
  r.energy_cost = r.energy_kwh * rates.energy_per_kwh;

  // Reuse credit (avoided-cost estimate, reported separately).
  const double avoided_s = acc.reuse_avoided_work.value + acc.reused_work.value;
  r.reuse_credit = avoided_s * gpu_rate;

  return r;
}

PricingPolicy& PricingPolicyStore::add(PricingPolicy p) {
  return store_[p.id] = std::move(p);
}

const PricingPolicy* PricingPolicyStore::find(const PricingPolicyId& id) const {
  const auto it = store_.find(id);
  return it == store_.end() ? nullptr : &it->second;
}

std::vector<PricingPolicy> PricingPolicyStore::all() const {
  std::vector<PricingPolicy> out;
  for (const auto& kv : store_) out.push_back(kv.second);
  return out;
}

}  // namespace iledger
