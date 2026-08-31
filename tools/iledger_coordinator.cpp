// iledger_coordinator.cpp
// A real coordinator OS process for the distributed proof.
//
// Usage: iledger_coordinator <port> <ledger_file>
//
// Runs the coordinator on 127.0.0.1:<port>. Waits for "SHUTDOWN" on stdin
// (or stdin EOF), then saves a snapshot of the authoritative ledger and exits.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include <cstdint>
#include <iostream>
#include <string>

#include "inference-ledger/coordinator.hpp"
#include "inference-ledger/persistence.hpp"

using namespace iledger;

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: iledger_coordinator <port> <ledger_file>\n";
    return 2;
  }
  const std::uint16_t port = static_cast<std::uint16_t>(std::stoi(argv[1]));
  const std::string ledger_path = argv[2];

  const LedgerId lid{0x494C4544474552ULL, 0x0000000000000001ULL};  // "ILEDGER"+1
  Coordinator coord(lid);
  std::string err;
  if (!coord.start(port, err)) {
    std::cerr << "start: " << err << "\n";
    return 1;
  }
  std::cout << "COORD " << port << "\n";
  std::cout.flush();

  // Wait for SHUTDOWN on stdin.
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "SHUTDOWN") break;
  }

  const auto snap = coord.ledger().snapshot();
  LedgerFingerprint fp = ledger_fingerprint(snap);
  if (!LedgerStore::save_snapshot(snap, lid, ledger_path, err)) {
    std::cerr << "save: " << err << "\n";
    coord.stop();
    return 1;
  }
  std::cout << "COORD_STOPPED " << snap.size() << " " << fp.to_string() << "\n";
  std::cout.flush();
  coord.stop();
  network_cleanup();
  return 0;
}
