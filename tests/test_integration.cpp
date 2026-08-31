// test_integration.cpp
// Runs the real multiprocess and CUDA proofs as OS subprocesses and verifies
// their exit codes. These are genuine OS-process / CUDA proofs, not in-process
// substitutes.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#include <cstdio>
#include <iostream>
#include <string>

int main() {
  int fails = 0;
  auto run_and_check = [&](const std::string& cmd, const std::string& marker) {
    std::cout << "[ RUN  ] " << cmd << "\n";
    std::cout.flush();
    const std::string full = cmd;
    const int rc = std::system((full + std::string(" > iledger_proof_out.txt 2>&1")).c_str());
    // Read the marker from the output file.
    FILE* f = std::fopen("iledger_proof_out.txt", "r");
    bool found = false;
    if (f) {
      char buf[4096];
      while (std::fgets(buf, sizeof(buf), f)) {
        if (std::string(buf).find(marker) != std::string::npos) found = true;
      }
      std::fclose(f);
    }
    const bool ok = (rc == 0) && found;
    std::cout << "[ " << (ok ? "PASS " : "FAIL") << " ] " << marker
              << " (rc " << rc << ", marker " << (found ? "found" : "missing") << ")\n";
    if (!ok) ++fails;
  };
  run_and_check("iledger_multiprocess.exe .", "MULTIPROCESS_PROOF PASS");
  run_and_check("iledger_cuda_proof.exe", "CUDA_PROOF PASS");
  std::cout << "\n== integration: " << (fails == 0 ? "PASS" : "FAIL") << " ==\n";
  return fails == 0 ? 0 : 1;
}
