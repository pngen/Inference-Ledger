// resource.hpp
// Resource-kind classification used to group ledger entries into cost/
// consumption buckets without collapsing distinct physical resources.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <string>

namespace iledger {

enum class ResourceKind : std::uint8_t {
  Compute = 1,       // GPU/CPU execution
  Memory = 2,        // generic memory allocation / reservation
  Kv = 3,            // KV-state allocation / reuse
  Tensor = 4,        // tensor state allocation / reuse
  Transfer = 5,      // H2D / D2H / inter-node
  Residency = 6,     // model / adapter / tensor residency
  Cache = 7,         // kernel / graph / persistent cache artifact
  Energy = 8,        // energy draw
  Cost = 9,          // cost adjustment
  Generic = 10       // fallback
};

inline const char* resource_kind_name(ResourceKind r) noexcept {
  switch (r) {
    case ResourceKind::Compute: return "compute";
    case ResourceKind::Memory: return "memory";
    case ResourceKind::Kv: return "kv";
    case ResourceKind::Tensor: return "tensor";
    case ResourceKind::Transfer: return "transfer";
    case ResourceKind::Residency: return "residency";
    case ResourceKind::Cache: return "cache";
    case ResourceKind::Energy: return "energy";
    case ResourceKind::Cost: return "cost";
    case ResourceKind::Generic: return "generic";
  }
  return "unknown";
}

}  // namespace iledger
