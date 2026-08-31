# Contributing to Inference Ledger

We welcome contributions from individuals and organizations.

## Contribution terms

All contributions are accepted on the terms of the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
No Contributor License Agreement (CLA) is required. By submitting a contribution
you agree that it may be distributed under the Apache License 2.0.

## How to contribute

1. Fork the repository and create a topic branch from `main`.
2. Make focused changes with clear commit messages.
3. Run the full build and test suite before opening a pull request.
4. Open a pull request describing the change, its motivation and any
   correctness/accounting implications.

## Build and test

Windows / MSVC 19.44+ is the primary target. Configure with CMake and build the
Release configuration, then run the unit and integration tests:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    ctest --test-dir build

The project builds with `/W4 /WX` on MSVC. A clean build must produce zero
warnings. A CUDA toolchain (CUDA 12.9+, sm_120) enables the CUDA accounting proof;
the network and multiprocess proofs require a local TCP loopback.

## Code standards

- C++20, deterministic, no undefined behavior, no data races.
- Every accounting value carries its provenance; never invent precision.
- Keep physical totals independent of pricing policy.
- Add or update tests for any behaviour change.

## Commit hygiene

Please do not add co-author trailers to commit messages unless the co-author
genuinely contributed and has consented.
