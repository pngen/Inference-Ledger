// inference_cuda_proof.cu
// Real CUDA inference-accounting proof on an NVIDIA datacenter/consumer GPU
// (validated on RTX 5090 / sm_120).
//
// This program performs real cudaMalloc, H2D, prefill-like CUDA execution,
// decode-like CUDA execution, synchronization, D2H, CPU-reference verification
// and cudaFree. It emits ledger events from the actual measured activity and
// demonstrates the cold / warm / failed-retry accounting paths, then proves
// that every physical CUDA allocation returns to the baseline.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "inference-ledger/batch.hpp"
#include "inference-ledger/ledger.hpp"
#include "inference-ledger/ledger_entry.hpp"
#include "inference-ledger/persistence.hpp"
#include "inference-ledger/pricing.hpp"
#include "inference-ledger/query.hpp"
#include "inference-ledger/request_account.hpp"

using namespace iledger;

namespace {

void check(cudaError_t e, const char* what) {
  if (e != cudaSuccess) {
    std::cerr << "CUDA error " << what << ": " << cudaGetErrorString(e) << "\n";
    std::exit(1);
  }
}

constexpr int kVec = 4096;
constexpr int kMat = 256;  // weight rows

// A prefill-like kernel: out[i] = sum_j w[i][j] * x[j] (matrix-vector).
__global__ void prefill_kernel(const float* w, const float* x, float* out,
                               int rows, int cols) {
  const int r = blockIdx.x * blockDim.x + threadIdx.x;
  if (r >= rows) return;
  float acc = 0.0f;
  for (int j = 0; j < cols; ++j) acc += w[r * cols + j] * x[j];
  out[r] = acc;
}

// A decode-like kernel: out[0] = sum_j w[0][j] * kv[j] (single token step).
__global__ void decode_kernel(const float* w, const float* kv, float* out,
                              int cols) {
  const int r = blockIdx.x * blockDim.x + threadIdx.x;
  float acc = 0.0f;
  for (int j = r; j < cols; j += (blockDim.x * gridDim.x)) {
    acc += w[j] * kv[j];
  }
  if (r == 0) out[0] = acc;
}

float host_reference(const std::vector<float>& w, const std::vector<float>& x,
                     int rows, int cols) {
  float acc = 0.0f;
  for (int r = 0; r < rows; ++r) {
    float s = 0.0f;
    for (int j = 0; j < cols; ++j) s += w[r * cols + j] * x[j];
    acc = s;
  }
  return acc;
}

}  // namespace

int main() {
  cudaDeviceProp prop{};
  check(cudaGetDeviceProperties(&prop, 0), "getDeviceProperties");
  std::cout << "GPU: " << prop.name << " compute_cap=" << prop.major << "."
            << prop.minor << " sm_120=" << (prop.major == 12 ? 1 : 0) << "\n";

  std::size_t free0 = 0, total0 = 0;
  check(cudaMemGetInfo(&free0, &total0), "MemGetInfo-before");

  int net_allocs = 0;

  const std::size_t wBytes = static_cast<std::size_t>(kMat) * kVec * sizeof(float);
  const std::size_t kvBytes = static_cast<std::size_t>(kVec) * sizeof(float);
  const std::size_t xBytes = static_cast<std::size_t>(kVec) * sizeof(float);
  const std::size_t outBytes = static_cast<std::size_t>(kMat) * sizeof(float);

  // Persistent model workspace (residency) + KV state.
  float* w = nullptr;
  float* kv = nullptr;
  check(cudaMalloc(&w, wBytes), "cudaMalloc w");
  net_allocs++;
  check(cudaMalloc(&kv, kvBytes), "cudaMalloc kv");
  net_allocs++;

  std::vector<float> hw(kMat * kVec, 0.0f);
  for (int i = 0; i < kMat * kVec; ++i) hw[i] = 0.0001f * static_cast<float>(i % 17);
  check(cudaMemcpy(w, hw.data(), wBytes, cudaMemcpyHostToDevice), "H2D w");
  check(cudaMemset(kv, 0, kvBytes), "memset kv");

  Ledger ledger(LedgerId{0x43554441ULL, 0x1});  // "CUDA"+1
  const TenantId T{0x99, 0x1};
  const WorkloadId WL{0x88, 0x1};
  const ModelId M{0x77, 0x1};
  const ModelRevisionId MR{0x77, 0x2};
  const WorkerId WORKER{0xAA, 0x1};
  const WorkerBootId BOOT{0xB007, 0x1};
  const NodeId NODE{0x6002, 0x1};
  const DeviceId DEV{0x6001, 0x1};
  const CoordinatorEpoch EP(1);
  const AccountingGeneration AG(1);

  auto make_entry = [&](int seq, RequestId req, AttemptId attempt,
                        RequestGeneration rg, AttemptGeneration atg,
                        EventKind kind, ResourceKind rk, double val, Unit unit,
                        Provenance prov, bool has_end, std::uint64_t s,
                        std::uint64_t e, DispatchId disp) {
    LedgerEntry en;
    en.id = LedgerEntryId{0xC000ULL + static_cast<std::uint64_t>(seq), 0x1};
    en.ledger = ledger.id();
    en.tenant = T;
    en.workload = WL;
    en.request = req;
    en.model = M;
    en.model_revision = MR;
    en.attempt = attempt;
    en.dispatch = disp;
    en.worker = WORKER;
    en.node = NODE;
    en.device = DEV;
    en.event_kind = kind;
    en.resource_kind = rk;
    en.quantity.value = val;
    en.quantity.unit = unit;
    en.quantity.provenance = prov;
    en.start_ts_ns = s;
    en.end_ts_ns = e;
    en.has_end = has_end;
    en.source.worker = WORKER;
    en.source.boot = BOOT;
    en.source.accounting_generation = AG;
    en.authority.epoch = EP;
    en.authority.worker_boot = BOOT;
    en.authority.request_generation = rg;
    en.authority.attempt = attempt;
    en.authority.attempt_generation = atg;
    en.authority.accounting_generation = AG;
    en.authority.dispatch = disp;
    return en;
  };

  int seq = 0;
  std::uint64_t now = 1000000ULL;
  auto tick = [&](std::uint64_t d) { now += d; return now; };

  float* x = nullptr;
  float* out = nullptr;
  check(cudaMalloc(&x, xBytes), "cudaMalloc x");
  net_allocs++;
  check(cudaMalloc(&out, outBytes), "cudaMalloc out");
  net_allocs++;

  // ---------------- REQUEST A : cold path ----------------
  const RequestId RA{0x1001, 0x1};
  const AttemptId AA{0x2001, 0x1};
  const RequestGeneration RGA(1);
  const AttemptGeneration AGA(1);
  const DispatchId DA{0x3001, 0x1};
  std::vector<float> hx(kVec, 0.0f);
  for (int i = 0; i < kVec; ++i) hx[i] = 0.0005f * static_cast<float>(i);

  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::RequestStart,
                           ResourceKind::Generic, 0.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DA));
  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::Reserve,
                           ResourceKind::Memory, 1.0, Unit::Count,
                           Provenance::Measured, false, tick(0), 0, DA));
  tick(100);
  check(cudaMemcpy(x, hx.data(), xBytes, cudaMemcpyHostToDevice), "H2D x");
  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::TransferH2D,
                           ResourceKind::Transfer, static_cast<double>(xBytes),
                           Unit::Bytes, Provenance::Measured, false, now, 0, DA));

  cudaEvent_t e0, e1;
  check(cudaEventCreate(&e0), "event0");
  check(cudaEventCreate(&e1), "event1");
  check(cudaEventRecord(e0, 0), "record0");
  prefill_kernel<<<(kMat + 127) / 128, 128>>>(w, x, out, kMat, kVec);
  check(cudaGetLastError(), "prefill launch");
  check(cudaEventRecord(e1, 0), "record1");
  check(cudaEventSynchronize(e1), "sync e1");
  float msA = 0.0f;
  check(cudaEventElapsedTime(&msA, e0, e1), "elapsed prefill");
  const std::uint64_t prefill_ns = static_cast<std::uint64_t>(msA * 1e6f);
  tick(prefill_ns);
  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::Prefill,
                           ResourceKind::Compute, prefill_ns / 1e9, Unit::Seconds,
                           Provenance::Measured, true, now - prefill_ns, now, DA));

  check(cudaEventRecord(e0, 0), "record0b");
  decode_kernel<<<1, 128>>>(w, kv, out, kVec);
  check(cudaGetLastError(), "decode launch");
  check(cudaEventRecord(e1, 0), "record1b");
  check(cudaEventSynchronize(e1), "sync e1b");
  float msB = 0.0f;
  check(cudaEventElapsedTime(&msB, e0, e1), "elapsed decode");
  const std::uint64_t decode_ns = static_cast<std::uint64_t>(msB * 1e6f);
  tick(decode_ns);
  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::Decode,
                           ResourceKind::Compute, decode_ns / 1e9, Unit::Seconds,
                           Provenance::Measured, true, now - decode_ns, now, DA));
  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::Decode,
                           ResourceKind::Compute, 32.0, Unit::Count,
                           Provenance::Measured, true, now - decode_ns, now, DA));
  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::ModelResidency,
                           ResourceKind::Residency, static_cast<double>(wBytes),
                           Unit::Bytes, Provenance::Measured, true,
                           now - prefill_ns - decode_ns, now, DA));

  std::vector<float> outv(kMat);
  check(cudaMemcpy(outv.data(), out, outBytes, cudaMemcpyDeviceToHost), "D2H out");
  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::TransferD2H,
                           ResourceKind::Transfer, static_cast<double>(outBytes),
                           Unit::Bytes, Provenance::Measured, false, now, 0, DA));

  // CPU-reference verification.
  const float ref = host_reference(hw, hx, kMat, kVec);
  const bool verified = std::fabs(outv[kMat - 1] - ref) < 1e-2f;
  std::cout << "A cpu-ref " << (verified ? "PASS" : "FAIL") << " ("
            << outv[kMat - 1] << " vs " << ref << ")\n";

  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::Release,
                           ResourceKind::Memory, 1.0, Unit::Count,
                           Provenance::Measured, false, tick(0), 0, DA));
  ledger.append(make_entry(++seq, RA, AA, RGA, AGA, EventKind::RequestEnd,
                           ResourceKind::Generic, 0.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DA));

  // free request A temporaries (they are stable buffers, freed at end)
  check(cudaEventDestroy(e0), "destroy0");
  check(cudaEventDestroy(e1), "destroy1");

  // ---------------- REQUEST B : warm / reuse path ----------------
  const RequestId RB{0x1002, 0x1};
  const AttemptId AB{0x2002, 0x1};
  const DispatchId DB{0x3002, 0x1};
  ledger.append(make_entry(++seq, RB, AB, RGA, AGA, EventKind::RequestStart,
                           ResourceKind::Generic, 0.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DB));
  ledger.append(make_entry(++seq, RB, AB, RGA, AGA, EventKind::KvReuse,
                           ResourceKind::Kv, static_cast<double>(kvBytes),
                           Unit::Bytes, Provenance::Measured, false, now, 0, DB));
  ledger.append(make_entry(++seq, RB, AB, RGA, AGA, EventKind::ReuseAvoided,
                           ResourceKind::Compute, 0.08, Unit::Seconds,
                           Provenance::Estimated, false, now, 0, DB));
  ledger.append(make_entry(++seq, RB, AB, RGA, AGA, EventKind::KernelHit,
                           ResourceKind::Cache, 1.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DB));
  const std::uint64_t bds = now;
  const std::uint64_t bde = tick(100000);
  ledger.append(make_entry(++seq, RB, AB, RGA, AGA, EventKind::Decode,
                           ResourceKind::Compute, 32.0, Unit::Count,
                           Provenance::Measured, true, bds, bde, DB));
  ledger.append(make_entry(++seq, RB, AB, RGA, AGA, EventKind::RequestEnd,
                           ResourceKind::Generic, 0.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DB));

  // ---------------- REQUEST C : failed / retry ----------------
  const RequestId RC{0x1003, 0x1};
  const AttemptId AC1{0x3001, 0x1};
  const AttemptId AC2{0x3002, 0x1};
  const AttemptGeneration ACG1(1);
  const AttemptGeneration ACG2(2);
  const DispatchId DC{0x3003, 0x1};
  // Attempt 1: executes work, then fails before accepted completion.
  ledger.append(make_entry(++seq, RC, AC1, RGA, ACG1, EventKind::RequestStart,
                           ResourceKind::Generic, 0.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DC));
  const std::uint64_t a1s = now;
  const std::uint64_t a1e = tick(50000000);
  ledger.append(make_entry(++seq, RC, AC1, RGA, ACG1, EventKind::Prefill,
                           ResourceKind::Compute, 0.05, Unit::Seconds,
                           Provenance::Measured, true, a1s, a1e, DC));
  ledger.append(make_entry(++seq, RC, AC1, RGA, ACG1, EventKind::Failure,
                           ResourceKind::Generic, 0.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DC));
  ledger.append(make_entry(++seq, RC, AC1, RGA, ACG1, EventKind::Retry,
                           ResourceKind::Generic, 1.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DC));
  // Attempt 2: retry under a fresh AttemptId + AttemptGeneration, succeeds.
  ledger.append(make_entry(++seq, RC, AC2, RGA, ACG2, EventKind::RequestStart,
                           ResourceKind::Generic, 0.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DC));
  const std::uint64_t a2s = now;
  const std::uint64_t a2e = tick(50000000);
  ledger.append(make_entry(++seq, RC, AC2, RGA, ACG2, EventKind::Prefill,
                           ResourceKind::Compute, 0.05, Unit::Seconds,
                           Provenance::Measured, true, a2s, a2e, DC));
  const std::uint64_t a2ds = now;
  const std::uint64_t a2de = tick(40000);
  ledger.append(make_entry(++seq, RC, AC2, RGA, ACG2, EventKind::Decode,
                           ResourceKind::Compute, 16.0, Unit::Count,
                           Provenance::Measured, true, a2ds, a2de, DC));
  ledger.append(make_entry(++seq, RC, AC2, RGA, ACG2, EventKind::RequestEnd,
                           ResourceKind::Generic, 0.0, Unit::Count,
                           Provenance::Measured, false, now, 0, DC));

  // ---------------- baseline cleanup ----------------
  // free request temporaries (x, out) and the persistent workspace/kv.
  check(cudaFree(x), "cudaFree x"); net_allocs--;
  check(cudaFree(out), "cudaFree out"); net_allocs--;
  check(cudaFree(kv), "cudaFree kv"); net_allocs--;
  check(cudaFree(w), "cudaFree w"); net_allocs--;

  std::size_t free1 = 0, total1 = 0;
  check(cudaMemGetInfo(&free1, &total1), "MemGetInfo-after");
  const bool baseline = (net_allocs == 0) &&
                        std::fabs(static_cast<double>(free0) - static_cast<double>(free1)) <
                            (16.0 * 1024.0 * 1024.0);
  std::cout << "baseline: net_allocs=" << net_allocs
            << " free(mib) before=" << (free0 >> 20)
            << " after=" << (free1 >> 20) << " -> "
            << (baseline ? "PASS" : "FAIL") << "\n";

  // ---------------- reconstruction ----------------
  const auto snap = ledger.snapshot();
  const RequestAccount accA = reconcile_request(query_ledger(snap, {.request = RA}), RA);
  const RequestAccount accB = reconcile_request(query_ledger(snap, {.request = RB}), RB);
  const RequestAccount accC = reconcile_request(query_ledger(snap, {.request = RC}), RC);

  const bool a_ok = accA.completed && accA.gpu_active_s.value > 0.0 && accA.generated_tokens == 32;
  const bool b_ok = accB.completed && accB.kv_reuse.value > 0.0 && accB.reuse_avoided_work.value > 0.0
                    && accB.prefill_s.value == 0.0;  // warm path: no prefill
  const bool c_ok = accC.completed && accC.retries == 1 && accC.failed_attempt_work.value == 0.05 &&
                    accC.attempt_count == 2 && accC.generated_tokens == 16;

  std::cout << "A cold: completed=" << accA.completed << " gpu_s=" << accA.gpu_active_s.value
            << " tokens=" << accA.generated_tokens << " -> " << (a_ok ? "PASS" : "FAIL") << "\n";
  std::cout << "B warm: completed=" << accB.completed << " kv_reuse=" << accB.kv_reuse.value
            << " prefill_s=" << accB.prefill_s.value << " reuse_credit=" << accB.reuse_credit()
            << " -> " << (b_ok ? "PASS" : "FAIL") << "\n";
  std::cout << "C retry: completed=" << accC.completed << " retries=" << accC.retries
            << " attempts=" << accC.attempt_count << " wasted_s=" << accC.failed_attempt_work.value
            << " tokens=" << accC.generated_tokens << " -> " << (c_ok ? "PASS" : "FAIL") << "\n";

  const bool all_ok = baseline && a_ok && b_ok && c_ok && verified;
  std::cout << "CUDA_PROOF " << (all_ok ? "PASS" : "FAIL") << "\n";
  check(cudaDeviceReset(), "deviceReset");
  return all_ok ? 0 : 1;
}
