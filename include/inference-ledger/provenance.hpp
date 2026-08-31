// provenance.hpp
// Accounting provenance: *how* a value was obtained.
//
// Inference Ledger never invents precision. Every accounting value carries a
// provenance tag, and the doctrine "missing measurements stay missing" is
// enforced by never reporting an unavailable value as if it were measured.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <string>

namespace iledger {

enum class Provenance : std::uint8_t {
  Measured,        // observed directly by this runtime / device telemetry
  Reported,        // supplied by an external, trusted source
  Derived,         // computed deterministically from other accounting values
  Estimated,       // computed from an explicit model (model identity persisted)
  Reconstructed,   // inferred from surviving evidence after a loss
  Unavailable      // known to be unobtainable on this path; stays missing
};

inline const char* provenance_name(Provenance p) noexcept {
  switch (p) {
    case Provenance::Measured: return "measured";
    case Provenance::Reported: return "reported";
    case Provenance::Derived: return "derived";
    case Provenance::Estimated: return "estimated";
    case Provenance::Reconstructed: return "reconstructed";
    case Provenance::Unavailable: return "unavailable";
  }
  return "unknown";
}

inline Provenance provenance_parse(const std::string& s) noexcept {
  if (s == "measured") return Provenance::Measured;
  if (s == "reported") return Provenance::Reported;
  if (s == "derived") return Provenance::Derived;
  if (s == "estimated") return Provenance::Estimated;
  if (s == "reconstructed") return Provenance::Reconstructed;
  return Provenance::Unavailable;
}

inline bool provenance_outer_tag_is_available(Provenance p) noexcept {
  // Only Measured/Reported imply the value genuinely came from observation.
  return p == Provenance::Measured || p == Provenance::Reported;
}

}  // namespace iledger
