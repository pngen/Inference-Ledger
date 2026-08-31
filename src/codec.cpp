#include "inference-ledger/codec.hpp"

#include <cmath>

namespace iledger {

namespace {

// CRC-32 (IEEE 802.3, reflected).
std::uint32_t crc32_impl(const std::uint8_t* data, std::size_t len) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) {
      const std::uint32_t mask = static_cast<std::uint32_t>(0u - (crc & 1u));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

template <typename Tag>
void put_id(ByteWriter& w, const Id128<Tag>& x) {
  std::uint8_t b[kIdentityBytes];
  x.to_bytes(b);
  w.bytes(b, kIdentityBytes);
}

template <typename Tag>
bool get_id(ByteReader& r, Id128<Tag>& x) {
  std::uint8_t b[kIdentityBytes];
  if (!r.bytes(b, kIdentityBytes)) return false;
  x = Id128<Tag>::from_bytes(b);
  return true;
}

bool is_physical_resource(ResourceKind k) {
  return k == ResourceKind::Compute || k == ResourceKind::Memory ||
         k == ResourceKind::Kv || k == ResourceKind::Tensor ||
         k == ResourceKind::Transfer || k == ResourceKind::Residency ||
         k == ResourceKind::Cache || k == ResourceKind::Energy;
}

bool valid_event_kind(std::uint8_t v) {
  return v >= static_cast<std::uint8_t>(EventKind::RequestStart) &&
         v <= static_cast<std::uint8_t>(EventKind::CostAdjustment);
}
bool valid_resource_kind(std::uint8_t v) {
  return v >= static_cast<std::uint8_t>(ResourceKind::Compute) &&
         v <= static_cast<std::uint8_t>(ResourceKind::Generic);
}
bool valid_unit(std::uint8_t v) {
  return v <= static_cast<std::uint8_t>(Unit::Dimensionless);
}
bool valid_provenance(std::uint8_t v) {
  return v <= static_cast<std::uint8_t>(Provenance::Unavailable);
}

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
  return crc32_impl(data, len);
}

bool encode_entry(const LedgerEntry& e, std::vector<std::uint8_t>& out) {
  // Reject NaN/Inf immediately (doctrine: no fabricated precision).
  if (!std::isfinite(e.quantity.value)) return false;
  if (is_physical_resource(e.resource_kind) && e.quantity.value < 0.0) return false;
  if (e.quantity.unit == Unit::Count && !e.quantity.is_integral()) return false;

  ByteWriter w;
  w.u8(kCodecEndianMarker);
  w.u32(kSchemaVersion);
  put_id(w, e.id);
  put_id(w, e.ledger);
  put_id(w, e.tenant);
  put_id(w, e.workload);
  put_id(w, e.request);
  put_id(w, e.model);
  put_id(w, e.model_revision);
  w.bool8(e.has_adapter);
  if (e.has_adapter) put_id(w, e.adapter);
  put_id(w, e.attempt);
  put_id(w, e.dispatch);
  put_id(w, e.worker);
  put_id(w, e.node);
  put_id(w, e.device);
  w.u8(static_cast<std::uint8_t>(e.event_kind));
  w.u8(static_cast<std::uint8_t>(e.resource_kind));
  std::uint64_t qbits = 0;
  std::memcpy(&qbits, &e.quantity.value, sizeof(qbits));
  w.u64(qbits);
  w.u8(static_cast<std::uint8_t>(e.quantity.unit));
  w.u8(static_cast<std::uint8_t>(e.quantity.provenance));
  w.u64(e.start_ts_ns);
  w.u64(e.end_ts_ns);
  w.bool8(e.has_end);
  put_id(w, e.source.worker);
  put_id(w, e.source.boot);
  w.u64(e.source.accounting_generation.value());
  w.u64(e.authority.epoch.value());
  put_id(w, e.authority.worker_boot);
  w.u64(e.authority.request_generation.value());
  w.u64(e.authority.accounting_generation.value());
  put_id(w, e.authority.attempt);
  w.u64(e.authority.attempt_generation.value());
  put_id(w, e.authority.dispatch);
  w.bool8(e.has_accounting_policy);
  if (e.has_accounting_policy) put_id(w, e.accounting_policy);
  w.bool8(e.has_pricing_policy);
  if (e.has_pricing_policy) put_id(w, e.pricing_policy);
  if (e.metadata.size() > 0xFFFFFFFFu) return false;
  w.u32(static_cast<std::uint32_t>(e.metadata.size()));
  for (const auto& kv : e.metadata) {
    w.string(kv.first);
    w.string(kv.second);
  }

  const auto& payload = w.data();
  out.clear();
  out.reserve(payload.size() + 8u);
  // Frame header: [u32 payload_len][u32 crc32]
  const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
  const std::uint32_t crc = crc32(payload.data(), payload.size());
  out.push_back(static_cast<std::uint8_t>(len >> 24));
  out.push_back(static_cast<std::uint8_t>(len >> 16));
  out.push_back(static_cast<std::uint8_t>(len >> 8));
  out.push_back(static_cast<std::uint8_t>(len));
  out.push_back(static_cast<std::uint8_t>(crc >> 24));
  out.push_back(static_cast<std::uint8_t>(crc >> 16));
  out.push_back(static_cast<std::uint8_t>(crc >> 8));
  out.push_back(static_cast<std::uint8_t>(crc));
  out.insert(out.end(), payload.begin(), payload.end());
  return true;
}

bool decode_entry(const std::uint8_t* data, std::size_t n, LedgerEntry& entry,
                  std::size_t& consumed) {
  // Need at least the 8-byte frame header.
  if (n < 8u) return false;
  const std::uint32_t len = (static_cast<std::uint32_t>(data[0]) << 24) |
                            (static_cast<std::uint32_t>(data[1]) << 16) |
                            (static_cast<std::uint32_t>(data[2]) << 8) |
                            static_cast<std::uint32_t>(data[3]);
  const std::uint32_t crc_want = (static_cast<std::uint32_t>(data[4]) << 24) |
                                 (static_cast<std::uint32_t>(data[5]) << 16) |
                                 (static_cast<std::uint32_t>(data[6]) << 8) |
                                 static_cast<std::uint32_t>(data[7]);
  const std::size_t total = 8u + len;
  if (total > n) return false;  // malformed length / truncation
  const std::uint8_t* payload = data + 8;
  const std::uint32_t crc_got = crc32(payload, len);
  if (crc_got != crc_want) return false;  // corruption

  ByteReader r(payload, len);
  std::uint8_t marker = 0;
  if (!r.u8(marker)) return false;
  if (marker != kCodecEndianMarker) return false;
  std::uint32_t ver = 0;
  if (!r.u32(ver)) return false;
  if (ver != kSchemaVersion) return false;  // unsupported version

  LedgerEntry e;
  if (!get_id(r, e.id)) return false;
  if (!get_id(r, e.ledger)) return false;
  if (!get_id(r, e.tenant)) return false;
  if (!get_id(r, e.workload)) return false;
  if (!get_id(r, e.request)) return false;
  if (!get_id(r, e.model)) return false;
  if (!get_id(r, e.model_revision)) return false;
  if (!r.bool8(e.has_adapter)) return false;
  if (e.has_adapter && !get_id(r, e.adapter)) return false;
  if (!get_id(r, e.attempt)) return false;
  if (!get_id(r, e.dispatch)) return false;
  if (!get_id(r, e.worker)) return false;
  if (!get_id(r, e.node)) return false;
  if (!get_id(r, e.device)) return false;
  std::uint8_t ek = 0, rk = 0;
  if (!r.u8(ek) || !r.u8(rk)) return false;
  if (!valid_event_kind(ek) || !valid_resource_kind(rk)) return false;
  e.event_kind = static_cast<EventKind>(ek);
  e.resource_kind = static_cast<ResourceKind>(rk);
  std::uint64_t qbits = 0;
  if (!r.u64(qbits)) return false;
  std::memcpy(&e.quantity.value, &qbits, sizeof(e.quantity.value));
  std::uint8_t qu = 0, qp = 0;
  if (!r.u8(qu) || !r.u8(qp)) return false;
  if (!valid_unit(qu) || !valid_provenance(qp)) return false;
  e.quantity.unit = static_cast<Unit>(qu);
  e.quantity.provenance = static_cast<Provenance>(qp);
  if (!r.u64(e.start_ts_ns)) return false;
  if (!r.u64(e.end_ts_ns)) return false;
  if (!r.bool8(e.has_end)) return false;
  if (!get_id(r, e.source.worker)) return false;
  if (!get_id(r, e.source.boot)) return false;
  std::uint64_t ag = 0;
  if (!r.u64(ag)) return false;
  e.source.accounting_generation = AccountingGeneration(ag);
  std::uint64_t epoch = 0, rg = 0, agg = 0, atg = 0;
  if (!r.u64(epoch)) return false;
  if (!get_id(r, e.authority.worker_boot)) return false;
  if (!r.u64(rg)) return false;
  if (!r.u64(agg)) return false;
  if (!get_id(r, e.authority.attempt)) return false;
  if (!r.u64(atg)) return false;
  if (!get_id(r, e.authority.dispatch)) return false;
  e.authority.epoch = CoordinatorEpoch(epoch);
  e.authority.request_generation = RequestGeneration(rg);
  e.authority.accounting_generation = AccountingGeneration(agg);
  e.authority.attempt_generation = AttemptGeneration(atg);
  if (!r.bool8(e.has_accounting_policy)) return false;
  if (e.has_accounting_policy && !get_id(r, e.accounting_policy)) return false;
  if (!r.bool8(e.has_pricing_policy)) return false;
  if (e.has_pricing_policy && !get_id(r, e.pricing_policy)) return false;
  std::uint32_t meta_count = 0;
  if (!r.u32(meta_count)) return false;
  for (std::uint32_t i = 0; i < meta_count; ++i) {
    std::string k, v;
    if (!r.string(k) || !r.string(v)) return false;
    e.metadata.emplace(std::move(k), std::move(v));
  }
  if (r.remaining() != 0) return false;  // no trailing garbage inside frame

  // Sanity validation on the decoded record.
  if (!std::isfinite(e.quantity.value)) return false;
  if (is_physical_resource(e.resource_kind) && e.quantity.value < 0.0) return false;

  entry = std::move(e);
  consumed = total;
  return true;
}

}  // namespace iledger
