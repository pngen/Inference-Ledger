// unit.hpp
// Unit model and typed quantities for Inference Ledger accounting.
//
// A Quantity is a value in a canonical base unit plus an explicit Unit and a
// Provenance. The unit is never inferred and never silently converted. Every
// aggregate keeps its unit; cost results keep their currency explicit.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "inference-ledger/provenance.hpp"

namespace iledger {

enum class Unit : std::uint8_t {
  Count,          // integers (tokens, entries, reservations, ...)
  Bytes,          // count of bytes
  ByteSeconds,    // duration x bytes (residency)
  Seconds,        // wall / execution time
  Nanoseconds,    // precise timing
  EnergyJoule,    // energy (J)
  EnergyWh,       // energy (Wh)
  Currency,       // money in the policy's declared minor units
  Dimensionless   // ratios, weights, efficiencies
};

inline const char* unit_name(Unit u) noexcept {
  switch (u) {
    case Unit::Count: return "count";
    case Unit::Bytes: return "bytes";
    case Unit::ByteSeconds: return "byte-seconds";
    case Unit::Seconds: return "seconds";
    case Unit::Nanoseconds: return "nanoseconds";
    case Unit::EnergyJoule: return "joules";
    case Unit::EnergyWh: return "wh";
    case Unit::Currency: return "currency";
    case Unit::Dimensionless: return "dimensionless";
  }
  return "unknown";
}

// Signature of a unit used for invariant checks and human output.
inline std::uint32_t unit_signature(Unit u) noexcept {
  return static_cast<std::uint32_t>(u) + 1u;
}

// A typed quantity: value in canonical base units, explicit unit, provenance.
struct Quantity {
  double value = 0.0;
  Unit unit = Unit::Dimensionless;
  Provenance provenance = Provenance::Unavailable;

  Quantity() = default;
  Quantity(double v, Unit u, Provenance p = Provenance::Measured) noexcept
      : value(v), unit(u), provenance(p) {}

  bool is_finite() const noexcept { return std::isfinite(value); }
  bool is_non_negative() const noexcept { return value >= 0.0; }
  bool is_integral() const noexcept { return std::floor(value) == value; }

  // Convenience constructors for common exact counts.
  static Quantity count(std::uint64_t v, Provenance p = Provenance::Measured) {
    return Quantity(static_cast<double>(v), Unit::Count, p);
  }
  static Quantity bytes(std::uint64_t v, Provenance p = Provenance::Measured) {
    return Quantity(static_cast<double>(v), Unit::Bytes, p);
  }
  static Quantity seconds(double v, Provenance p = Provenance::Measured) {
    return Quantity(v, Unit::Seconds, p);
  }
};

// Identical comparing only finite, same-unit quantities. Two Quantities are
// considered equal when they carry the same value and unit.
inline bool same_unit(const Quantity& a, const Quantity& b) noexcept {
  return a.unit == b.unit;
}

// Renders a quantity with unit and provenance, e.g. "1234 bytes [measured]".
std::string to_string(const Quantity& q);

}  // namespace iledger
