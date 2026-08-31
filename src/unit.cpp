#include "inference-ledger/unit.hpp"
namespace iledger {
std::string to_string(const Quantity& q) {
  std::string out = std::to_string(q.value);
  out += " ";
  out += unit_name(q.unit);
  out += " [";
  out += provenance_name(q.provenance);
  out += "]";
  return out;
}
}  // namespace iledger
