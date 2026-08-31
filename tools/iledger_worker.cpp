// iledger_worker.cpp
// A real accounting-source (worker) OS process for the distributed proof.
//
// Usage: iledger_worker <host> <port> <worker_id_hex> <boot_id_hex> <script_file>
//
// Connects, registers with a fresh WorkerBootId, then submits each ledger
// entry encoded in the (binary) script file. Prints a status line per entry
// and forwards any authority Roll it receives.
//
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "inference-ledger/codec.hpp"
#include "inference-ledger/identity.hpp"
#include "inference-ledger/protocol.hpp"

using namespace iledger;

int main(int argc, char** argv) {
  if (argc != 7) {
    std::cerr << "usage: iledger_worker <host> <port> <worker_id_hex> "
                 "<boot_id_hex> <script_file> <hold>\n";
    return 2;
  }
  const bool hold = (std::string(argv[6]) == "1");
  const std::string host = argv[1];
  const std::uint16_t port = static_cast<std::uint16_t>(std::stoi(argv[2]));
  const auto worker = WorkerId::parse(argv[3]);
  const auto boot = WorkerBootId::parse(argv[4]);
  if (!worker || !boot) {
    std::cerr << "bad identity\n";
    return 2;
  }

  std::string err;
  if (!network_init(err)) { std::cerr << "network_init: " << err << "\n"; return 1; }

  FramedSocket sock;
  if (!connect_tcp(host, port, sock, err)) {
    std::cerr << "connect: " << err << "\n";
    return 1;
  }

  // Register.
  Message reg;
  reg.type = MessageType::Register;
  reg.reg.worker = *worker;
  reg.reg.boot = *boot;
  if (!sock.send_message(reg, err)) { std::cerr << "send reg: " << err << "\n"; return 1; }
  Message regack;
  if (!sock.recv_message(regack, err)) { std::cerr << "recv regack: " << err << "\n"; return 1; }
  if (regack.type != MessageType::RegisterAck || !regack.reg_ack.ok) {
    std::cerr << "registration rejected: " << regack.reg_ack.reason << "\n";
    return 1;
  }
  std::cout << "REG " << regack.reg_ack.epoch.value() << " "
            << regack.reg_ack.accounting_generation.value() << "\n";
  std::cout.flush();

  CoordinatorEpoch epoch = regack.reg_ack.epoch;
  AccountingGeneration ag = regack.reg_ack.accounting_generation;

  // Read the binary script of entry frames.
  std::ifstream in(argv[5], std::ios::binary);
  if (!in) { std::cerr << "cannot open script\n"; return 1; }
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
  std::size_t pos = 0;
  int index = 0;
  while (pos < data.size()) {
    LedgerEntry e;
    std::size_t consumed = 0;
    if (!decode_entry(data.data() + pos, data.size() - pos, e, consumed)) {
      std::cerr << "bad script frame " << index << "\n";
      return 1;
    }
    pos += consumed;
    Message submit;
    submit.type = MessageType::EventSubmit;
    submit.event.entry = e;
    if (!sock.send_message(submit, err)) {
      std::cerr << "send submit: " << err << "\n";
      return 1;
    }
    // Await an ack, handling any interleaved Roll.
    bool acked = false;
    while (!acked) {
      Message reply;
      if (!sock.recv_message(reply, err)) {
        std::cerr << "recv reply: " << err << "\n";
        return 1;
      }
      if (reply.type == MessageType::Roll) {
        epoch = reply.roll.epoch;
        ag = reply.roll.accounting_generation;
        std::cout << "ROLL " << epoch.value() << " " << ag.value() << "\n";
        std::cout.flush();
      } else if (reply.type == MessageType::EventAck) {
        std::cout << "ACK " << index << " "
                  << static_cast<int>(reply.event_ack.status) << " "
                  << reply.event_ack.reason << "\n";
        std::cout.flush();
        acked = true;
      } else {
        std::cerr << "unexpected message\n";
        return 1;
      }
    }
    ++index;
  }

  std::cout << "WORKER_DONE " << worker->to_string() << "\n";
  std::cout.flush();
  if (hold) {
    // Stay alive until the orchestrator terminates us as an OS process.
    for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  sock.close();
  network_cleanup();
  return 0;
}
