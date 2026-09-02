cmake_minimum_required(VERSION 3.24)

# Default for local builds; release builds pass -DXRDHOVER_VERSION=<tag> so the
# binary matches the GitHub release / tarball name.
if(NOT DEFINED XRDHOVER_VERSION)
  set(XRDHOVER_VERSION "0.4.0")
endif()

project(xrdhover
  VERSION ${XRDHOVER_VERSION}
  DESCRIPTION "XRootD remote-read traffic generator for CMS / WLCG data challenges"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Runtime: XRootD 6 client (libXrdCl.so.6). Grid jobs get that library from
# CMSSW_20_1_0_pre2 (el9). The release binary itself must be built on el9.
find_package(XRootD 6 REQUIRED COMPONENTS CLIENT)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

include(FetchContent)
FetchContent_Declare(cli11
  URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.5.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(cli11)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

set(XRDHOVER_SOURCES
  src/file_session.cc
  src/log.cc
  src/read_command.cc
  src/run_command.cc
  src/run_config.cc
  src/units.cc
  src/token_bucket.cc
  src/inflight.cc
  src/error_classifier.cc
  src/scheduler.cc
  src/metrics.cc
  src/file_sink.cc
  src/prom_encode.cc
  src/pushgateway_sink.cc
  src/chirp_sink.cc
  src/soft_fault_log.cc
  src/workload_spec.cc
  src/auth_check.cc
  src/probe_command.cc
  src/report_command.cc
  src/workload_run_command.cc
  src/xrdcl_env.cc
  src/site_map.cc
  src/sitename_resolver.cc)

add_executable(xrdhover
  src/main.cc
  ${XRDHOVER_SOURCES})

target_include_directories(xrdhover PRIVATE include)

target_compile_definitions(xrdhover PRIVATE
  XRDHOVER_VERSION="${PROJECT_VERSION}")

target_compile_options(xrdhover PRIVATE -Wall -Wextra)

target_link_libraries(xrdhover PRIVATE
  XRootD::XrdCl
  CLI11::CLI11
  nlohmann_json::nlohmann_json
  CURL::libcurl
  OpenSSL::Crypto)

install(TARGETS xrdhover RUNTIME DESTINATION bin)

option(XRDHOVER_ENABLE_TESTS "Build unit tests" ON)

if(XRDHOVER_ENABLE_TESTS)
  enable_testing()

  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(xrdhover-tests
    tests/test_units.cc
    tests/test_token_bucket.cc
    tests/test_error_classifier.cc
    tests/test_auto_max_bytes.cc
    tests/test_metrics.cc
    tests/test_file_sink.cc
    tests/test_prom_encode.cc
    tests/test_soft_fault.cc
    tests/test_random_io.cc
    tests/test_workload_spec.cc
    tests/test_auth_check.cc
    tests/test_probe_command.cc
    tests/test_workload_run_command.cc
    tests/test_site_map.cc
    tests/test_sitename_resolver.cc
    tests/test_report_command.cc
    tests/test_push_group.cc
    tests/stub_run_command.cc
    src/units.cc
    src/log.cc
    src/token_bucket.cc
    src/error_classifier.cc
    src/run_config.cc
    src/metrics.cc
    src/file_sink.cc
    src/prom_encode.cc
    src/soft_fault_log.cc
    src/workload_spec.cc
    src/auth_check.cc
    src/probe_command.cc
    src/report_command.cc
    src/workload_run_command.cc
    src/xrdcl_env.cc
    src/inflight.cc
    src/file_session.cc
    src/site_map.cc
    src/sitename_resolver.cc)

  target_include_directories(xrdhover-tests PRIVATE include)
  target_compile_options(xrdhover-tests PRIVATE -Wall -Wextra)
  target_link_libraries(xrdhover-tests PRIVATE
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    XRootD::XrdCl
    OpenSSL::Crypto)

  include(GoogleTest)
  gtest_discover_tests(xrdhover-tests)
endif()
