// pricing.hpp
// Typed, versioned, immutable pricing policies and deterministic cost
// computation. No hard-coded cloud-vendor pricing: every rate is supplied by
// the operator. Changing a rate creates a new policy generation; historical
// physical consumption never changes.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "inference-ledger/identity.hpp"
#include "inference-ledger/request_account.hpp"

namespace iledger {

// A set of explicit rates. All rates are double; unit conventions are fixed by
// the field name. Unknown/unsupported axes use 0.0 (which means "not billed"
// rather than "free"), and the policy names its currency.
struct PricingRates {
  double gpu_per_second = 0.0;
  double cpu_per_second = 0.0;
  double memory_per_gib_second = 0.0;      // $ per GiB-second
  double host_memory_per_gib_second = 0.0;
  double pinned_memory_per_gib_second = 0.0;
  double transfer_per_gib = 0.0;           // $ per GiB transferred
  double storage_per_gib_second = 0.0;
  double persistent_cache_cost = 0.0;      // fixed $ per cache artifact
  double energy_per_kwh = 0.0;             // $ per kWh
  std::map<std::string, double> custom_device_per_second;

  bool all_finite() const;
};

struct PricingPolicy {
  PricingPolicyId id{};
  std::uint64_t generation = 0;   // immutable per generation
  std::string currency;
  std::string name;
  PricingRates rates{};

  bool operator==(const PricingPolicy& o) const noexcept {
    return id == o.id && generation == o.generation && currency == o.currency &&
           name == o.name && rates.all_finite() == o.rates.all_finite() &&
           rates.gpu_per_second == o.rates.gpu_per_second &&
           rates.cpu_per_second == o.rates.cpu_per_second &&
           rates.memory_per_gib_second == o.rates.memory_per_gib_second &&
           rates.host_memory_per_gib_second == o.rates.host_memory_per_gib_second &&
           rates.pinned_memory_per_gib_second == o.rates.pinned_memory_per_gib_second &&
           rates.transfer_per_gib == o.rates.transfer_per_gib &&
           rates.storage_per_gib_second == o.rates.storage_per_gib_second &&
           rates.persistent_cache_cost == o.rates.persistent_cache_cost &&
           rates.energy_per_kwh == o.rates.energy_per_kwh &&
           rates.custom_device_per_second == o.rates.custom_device_per_second;
  }
};

// Cost breakdown produced by applying a pricing policy to a physical account.
// The physical account is never mutated: physical totals stay independent of
// pricing policy.
struct CostResult {
  PricingPolicyId policy_id{};
  std::uint64_t policy_generation = 0;
  std::string currency;

  double compute_cost = 0.0;
  double cpu_compute_cost = 0.0;
  double residency_cost = 0.0;
  double transfer_cost = 0.0;
  double storage_cache_cost = 0.0;
  double energy_cost = 0.0;

  // Useful / wasted split of compute cost.
  double useful_work_cost = 0.0;
  double wasted_work_cost = 0.0;      // failed attempt + rejected speculation
  double cancelled_cost = 0.0;
  double retry_cost = 0.0;            // sub-component of wasted_work_cost
  double rejected_spec_cost = 0.0;    // sub-component of wasted_work_cost
  double recomputed_cost = 0.0;
  double reuse_credit = 0.0;          // avoided-cost estimate (money saved)

  double energy_kwh = 0.0;

  // Exact total of the billed parts (useful + wasted + cancelled plus the
  // non-compute buckets). reuse_credit is reported but not subtracted.
  double total() const {
    return compute_cost + residency_cost + transfer_cost +
           storage_cache_cost + energy_cost;
  }
  // Reconcile: billed total must equal the sum of its declared components.
  bool reconciles() const;

  // A store for deterministic JSON / text rendering.
  double useful_fraction() const;
};

// Apply a policy to a physical account. Deterministic; does not modify the
// account. Replaying the same account+policy reproduces identical cost.
CostResult apply_pricing(const RequestAccount& acc,
                         const PricingPolicy& policy);

// A versioned, read-mostly store of policies. Each stored policy carries its
// own id and immutable generation; adding a policy never mutates an existing
// one.
class PricingPolicyStore {
 public:
  PricingPolicy& add(PricingPolicy p);
  const PricingPolicy* find(const PricingPolicyId& id) const;
  std::vector<PricingPolicy> all() const;

 private:
  std::map<PricingPolicyId, PricingPolicy> store_;
};

}  // namespace iledger
