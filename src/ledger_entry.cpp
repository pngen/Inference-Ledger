#include "inference-ledger/ledger_entry.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace iledger {
namespace {

// A small, easily-auditable 128-bit avalanche mixer used to derive a stable
// content identity. It is not a cryptographic primitive; it only needs to be
// deterministic and well-distributed to give idempotent replay a canonical id.
struct Mixer {
  std::uint64_t hi = 0xCBF29CE484222325ULL;
  std::uint64_t lo = 0x9E3779B97F4A7C15ULL;

  void feed(std::uint64_t v) {
    hi ^= v + 0x9E3779B97F4A7C15ULL + (hi << 6) + (hi >> 2);
    lo = (lo ^ (v * 0x100000001B3ULL)) * 0x9E3779B97F4A7C15ULL;
  }
  void feed_bytes(const std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) feed(static_cast<std::uint64_t>(p[i]));
  }
  template <typename Tag>
  void feed_id(const Id128<Tag>& x) {
    feed(x.hi());
    feed(x.lo());
  }
};

}  // namespace

LedgerEntryId derive_entry_id(const LedgerEntry& e) {
  Mixer m;
  m.feed_id(e.ledger);
  m.feed_id(e.tenant);
  m.feed_id(e.workload);
  m.feed_id(e.request);
  m.feed_id(e.model);
  m.feed_id(e.model_revision);
  m.feed(static_cast<std::uint64_t>(e.event_kind));
  m.feed(static_cast<std::uint64_t>(e.resource_kind));
  m.feed(static_cast<std::uint64_t>(e.source.worker.hi()) ^ e.source.worker.lo());
  m.feed(static_cast<std::uint64_t>(e.source.boot.hi()) ^ e.source.boot.lo());
  m.feed(e.source.accounting_generation.value());
  m.feed(e.authority.epoch.value());
  m.feed(e.authority.accounting_generation.value());
  m.feed(e.authority.request_generation.value());
  m.feed(e.authority.attempt_generation.value());
  m.feed(static_cast<std::uint64_t>(e.authority.attempt.hi()) ^ e.authority.attempt.lo());
  m.feed(e.start_ts_ns);
  std::uint64_t qbits = 0;
  std::memcpy(&qbits, &e.quantity.value, sizeof(qbits));
  m.feed(qbits);
  return LedgerEntryId{m.hi, m.lo};
}

}  // namespace iledger
