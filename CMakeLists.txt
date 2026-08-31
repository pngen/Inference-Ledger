cmake_minimum_required(VERSION 3.25)
project(InferenceLedger VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckLanguage)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(inference_ledger STATIC
  src/unit.cpp
  src/codec.cpp
  src/ledger_entry.cpp
  src/request_account.cpp
  src/pricing.cpp
  src/batch.cpp
  src/persistence.cpp
  src/protocol.cpp
  src/coordinator.cpp
  src/query.cpp
)

target_include_directories(inference_ledger PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(WIN32)
  target_compile_definitions(inference_ledger PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(WIN32)
  target_link_libraries(inference_ledger PUBLIC ws2_32)
endif()

if(MSVC)
  target_compile_options(inference_ledger PRIVATE /W4 /WX)
else()
  target_compile_options(inference_ledger PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Common target hardening -------------------------------------------
function(iledger_set_defaults target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endfunction()

# --- CLI / tools ---------------------------------------------------------
add_executable(iledger_coordinator tools/iledger_coordinator.cpp)
target_link_libraries(iledger_coordinator PRIVATE inference_ledger)
iledger_set_defaults(iledger_coordinator)

add_executable(iledger_worker tools/iledger_worker.cpp)
target_link_libraries(iledger_worker PRIVATE inference_ledger)
iledger_set_defaults(iledger_worker)

add_executable(iledger_multiprocess tools/iledger_multiprocess.cpp)
target_link_libraries(iledger_multiprocess PRIVATE inference_ledger)
iledger_set_defaults(iledger_multiprocess)

# --- CUDA proof (requires real CUDA toolchain + sm_120 device) ----------
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  add_executable(iledger_cuda_proof cuda/inference_cuda_proof.cu)
  target_link_libraries(iledger_cuda_proof PRIVATE inference_ledger)
  target_include_directories(iledger_cuda_proof PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_target_properties(iledger_cuda_proof PROPERTIES CUDA_ARCHITECTURES 120
                        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  if(MSVC)
    target_compile_options(iledger_cuda_proof PRIVATE "-Xcompiler=/W4,/WX")
  else()
    target_compile_options(iledger_cuda_proof PRIVATE "-Xcompiler=-Wall,-Wextra,-Wpedantic")
  endif()
endif()

# --- Tests --------------------------------------------------------------
enable_testing()

add_executable(iledger_tests
  tests/test_main.cpp
  tests/test_core.cpp
  tests/test_accounting.cpp
  tests/test_persistence.cpp
  tests/test_property.cpp
  tests/test_concurrency.cpp
)
target_link_libraries(iledger_tests PRIVATE inference_ledger)
iledger_set_defaults(iledger_tests)
add_test(NAME unit_tests COMMAND iledger_tests)

if(CMAKE_CUDA_COMPILER)
  add_executable(iledger_integration_test tests/test_integration.cpp)
  target_link_libraries(iledger_integration_test PRIVATE inference_ledger)
  iledger_set_defaults(iledger_integration_test)
  add_test(NAME integration_tests COMMAND iledger_integration_test)
  set_tests_properties(integration_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# --- CLI / benchmark -----------------------------------------------------
add_executable(iledger tools/iledger_cli.cpp)
target_link_libraries(iledger PRIVATE inference_ledger)
iledger_set_defaults(iledger)

add_executable(iledger_benchmark tools/iledger_benchmark.cpp)
target_link_libraries(iledger_benchmark PRIVATE inference_ledger)
target_include_directories(iledger_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
iledger_set_defaults(iledger_benchmark)

# --- Install / export --------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS inference_ledger EXPORT InferenceLedgerTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION include)

install(EXPORT InferenceLedgerTargets
  FILE InferenceLedgerTargets.cmake
  NAMESPACE InferenceLedger::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InferenceLedger)

configure_package_config_file(
  cmake/InferenceLedgerConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/InferenceLedgerConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InferenceLedger)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/InferenceLedgerConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/InferenceLedgerConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/InferenceLedgerConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InferenceLedger)

# --- Examples -----------------------------------------------------------
add_executable(iledger_examples examples/examples.cpp)
target_link_libraries(iledger_examples PRIVATE inference_ledger)
iledger_set_defaults(iledger_examples)
