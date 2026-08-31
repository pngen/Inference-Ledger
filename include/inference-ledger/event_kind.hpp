// event_kind.hpp
// Ledger event categories. Values are stable wire values (never renumbered).
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <string>

namespace iledger {

enum class EventKind : std::uint8_t {
  RequestStart = 1,
  RequestEnd = 2,
  Queue = 3,
  BatchWait = 4,
  Prefill = 5,
  Decode = 6,
  SpeculationProposed = 7,
  SpeculationAccepted = 8,
  SpeculationRejected = 9,
  GpuExecution = 10,
  CpuExecution = 11,
  KvAllocate = 12,
  KvRelease = 13,
  KvReuse = 14,
  TensorAllocate = 15,
  TensorRelease = 16,
  TensorReuse = 17,
  ModelResidency = 18,
  AdapterResidency = 19,
  KernelHit = 20,
  KernelMiss = 21,
  GraphHit = 22,
  GraphMiss = 23,
  TransferH2D = 24,
  TransferD2H = 25,
  TransferInterNode = 26,
  CacheRead = 27,
  CacheWrite = 28,
  Reserve = 29,
  Release = 30,
  Retry = 31,
  Failure = 32,
  Cancellation = 33,
  Recompute = 34,
  ReuseAvoided = 35,
  Energy = 36,
  CostAdjustment = 37
};

inline const char* event_kind_name(EventKind k) noexcept {
  switch (k) {
    case EventKind::RequestStart: return "REQUEST_START";
    case EventKind::RequestEnd: return "REQUEST_END";
    case EventKind::Queue: return "QUEUE";
    case EventKind::BatchWait: return "BATCH_WAIT";
    case EventKind::Prefill: return "PREFILL";
    case EventKind::Decode: return "DECODE";
    case EventKind::SpeculationProposed: return "SPECULATION_PROPOSED";
    case EventKind::SpeculationAccepted: return "SPECULATION_ACCEPTED";
    case EventKind::SpeculationRejected: return "SPECULATION_REJECTED";
    case EventKind::GpuExecution: return "GPU_EXECUTION";
    case EventKind::CpuExecution: return "CPU_EXECUTION";
    case EventKind::KvAllocate: return "KV_ALLOCATE";
    case EventKind::KvRelease: return "KV_RELEASE";
    case EventKind::KvReuse: return "KV_REUSE";
    case EventKind::TensorAllocate: return "TENSOR_ALLOCATE";
    case EventKind::TensorRelease: return "TENSOR_RELEASE";
    case EventKind::TensorReuse: return "TENSOR_REUSE";
    case EventKind::ModelResidency: return "MODEL_RESIDENCY";
    case EventKind::AdapterResidency: return "ADAPTER_RESIDENCY";
    case EventKind::KernelHit: return "KERNEL_HIT";
    case EventKind::KernelMiss: return "KERNEL_MISS";
    case EventKind::GraphHit: return "GRAPH_HIT";
    case EventKind::GraphMiss: return "GRAPH_MISS";
    case EventKind::TransferH2D: return "TRANSFER_H2D";
    case EventKind::TransferD2H: return "TRANSFER_D2H";
    case EventKind::TransferInterNode: return "TRANSFER_INTER_NODE";
    case EventKind::CacheRead: return "CACHE_READ";
    case EventKind::CacheWrite: return "CACHE_WRITE";
    case EventKind::Reserve: return "RESERVE";
    case EventKind::Release: return "RELEASE";
    case EventKind::Retry: return "RETRY";
    case EventKind::Failure: return "FAILURE";
    case EventKind::Cancellation: return "CANCELLATION";
    case EventKind::Recompute: return "RECOMPUTE";
    case EventKind::ReuseAvoided: return "REUSE_AVOIDED";
    case EventKind::Energy: return "ENERGY";
    case EventKind::CostAdjustment: return "COST_ADJUSTMENT";
  }
  return "UNKNOWN";
}

inline EventKind event_kind_parse(const std::string& s) noexcept {
  if (s == "REQUEST_START") return EventKind::RequestStart;
  if (s == "REQUEST_END") return EventKind::RequestEnd;
  if (s == "QUEUE") return EventKind::Queue;
  if (s == "BATCH_WAIT") return EventKind::BatchWait;
  if (s == "PREFILL") return EventKind::Prefill;
  if (s == "DECODE") return EventKind::Decode;
  if (s == "SPECULATION_PROPOSED") return EventKind::SpeculationProposed;
  if (s == "SPECULATION_ACCEPTED") return EventKind::SpeculationAccepted;
  if (s == "SPECULATION_REJECTED") return EventKind::SpeculationRejected;
  if (s == "GPU_EXECUTION") return EventKind::GpuExecution;
  if (s == "CPU_EXECUTION") return EventKind::CpuExecution;
  if (s == "KV_ALLOCATE") return EventKind::KvAllocate;
  if (s == "KV_RELEASE") return EventKind::KvRelease;
  if (s == "KV_REUSE") return EventKind::KvReuse;
  if (s == "TENSOR_ALLOCATE") return EventKind::TensorAllocate;
  if (s == "TENSOR_RELEASE") return EventKind::TensorRelease;
  if (s == "TENSOR_REUSE") return EventKind::TensorReuse;
  if (s == "MODEL_RESIDENCY") return EventKind::ModelResidency;
  if (s == "ADAPTER_RESIDENCY") return EventKind::AdapterResidency;
  if (s == "KERNEL_HIT") return EventKind::KernelHit;
  if (s == "KERNEL_MISS") return EventKind::KernelMiss;
  if (s == "GRAPH_HIT") return EventKind::GraphHit;
  if (s == "GRAPH_MISS") return EventKind::GraphMiss;
  if (s == "TRANSFER_H2D") return EventKind::TransferH2D;
  if (s == "TRANSFER_D2H") return EventKind::TransferD2H;
  if (s == "TRANSFER_INTER_NODE") return EventKind::TransferInterNode;
  if (s == "CACHE_READ") return EventKind::CacheRead;
  if (s == "CACHE_WRITE") return EventKind::CacheWrite;
  if (s == "RESERVE") return EventKind::Reserve;
  if (s == "RELEASE") return EventKind::Release;
  if (s == "RETRY") return EventKind::Retry;
  if (s == "FAILURE") return EventKind::Failure;
  if (s == "CANCELLATION") return EventKind::Cancellation;
  if (s == "RECOMPUTE") return EventKind::Recompute;
  if (s == "REUSE_AVOIDED") return EventKind::ReuseAvoided;
  if (s == "ENERGY") return EventKind::Energy;
  if (s == "COST_ADJUSTMENT") return EventKind::CostAdjustment;
  return EventKind::RequestStart;  // unknown maps to a benign value
}

}  // namespace iledger
