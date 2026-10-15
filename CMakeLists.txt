cmake_minimum_required(VERSION 3.24)
project(ycnet LANGUAGES CXX)

option(ycnet_build_comparisons "Build pinned KCP protocol comparison" OFF)
if(ycnet_build_comparisons)
  set(ycnet_kcp_source_dir "${PROJECT_SOURCE_DIR}/.dependencies/kcp" CACHE PATH "Pinned KCP checkout")
  if(NOT EXISTS "${ycnet_kcp_source_dir}/ikcp.c")
    message(FATAL_ERROR "Run tools/run_protocol_comparison.ps1 to fetch the pinned KCP source")
  endif()
  execute_process(COMMAND git -C "${ycnet_kcp_source_dir}" rev-parse HEAD
    OUTPUT_VARIABLE kcp_commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(NOT kcp_commit STREQUAL "b1a7a2101dcbb96017681a500d6b82bbe5a88766")
    message(FATAL_ERROR "KCP baseline must use commit b1a7a2101dcbb96017681a500d6b82bbe5a88766")
  endif()
  set_source_files_properties(${ycnet_kcp_source_dir}/ikcp.c PROPERTIES LANGUAGE CXX)
  add_library(kcp_baseline STATIC ${ycnet_kcp_source_dir}/ikcp.c)
  target_include_directories(kcp_baseline PUBLIC ${ycnet_kcp_source_dir})
endif()

add_library(ycnet INTERFACE)
target_include_directories(ycnet INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(ycnet INTERFACE cxx_std_23)
if(MSVC)
  target_compile_options(ycnet INTERFACE /utf-8)
endif()

option(ycnet_build_tests "Build network verification executables" ON)
if(ycnet_build_tests)
  enable_testing()

  add_executable(rudp_tests tests/rudp_tests.cpp)
  target_link_libraries(rudp_tests PRIVATE ycnet)
  target_compile_options(rudp_tests PRIVATE /std:c++latest)
  add_test(NAME rudp_tests COMMAND rudp_tests)

  add_executable(reliability_policy_tests tests/reliability_policy_tests.cpp)
  target_link_libraries(reliability_policy_tests PRIVATE ycnet)
  add_test(NAME reliability_policy_tests COMMAND reliability_policy_tests)

  add_executable(sequence_reuse_tests tests/sequence_reuse_tests.cpp)
  target_link_libraries(sequence_reuse_tests PRIVATE ycnet)
  add_test(NAME sequence_reuse_tests COMMAND sequence_reuse_tests)

  add_executable(rio_smoke tests/rio_smoke.cpp)
  target_link_libraries(rio_smoke PRIVATE ycnet)
  target_include_directories(rio_smoke PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(rio_smoke PRIVATE /std:c++latest)
  add_test(NAME rio_smoke COMMAND rio_smoke)

  add_executable(ack_loss_tests tests/ack_loss_tests.cpp)
  target_link_libraries(ack_loss_tests PRIVATE ycnet)
  target_include_directories(ack_loss_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(ack_loss_tests PRIVATE /std:c++latest)
  add_test(NAME ack_loss_tests COMMAND ack_loss_tests)

  add_executable(rio_ack_loss_smoke tests/rio_ack_loss_smoke.cpp)
  target_link_libraries(rio_ack_loss_smoke PRIVATE ycnet)
  target_include_directories(rio_ack_loss_smoke PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(rio_ack_loss_smoke PRIVATE /std:c++latest)
  add_test(NAME rio_ack_loss_smoke COMMAND rio_ack_loss_smoke)

  add_executable(send_window_tests tests/send_window_tests.cpp)
  target_link_libraries(send_window_tests PRIVATE ycnet)
  target_include_directories(send_window_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(send_window_tests PRIVATE /std:c++latest)
  add_test(NAME send_window_tests COMMAND send_window_tests)

  add_executable(network_loss_tests tests/network_loss_tests.cpp)
  target_link_libraries(network_loss_tests PRIVATE ycnet)
  target_include_directories(network_loss_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR}/benchmarks/include)
  target_compile_options(network_loss_tests PRIVATE /std:c++latest)
  add_test(NAME network_loss_tests COMMAND network_loss_tests)

  add_executable(rio_multi_client_smoke tests/rio_multi_client_smoke.cpp)
  target_link_libraries(rio_multi_client_smoke PRIVATE ycnet)
  target_include_directories(rio_multi_client_smoke PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(rio_multi_client_smoke PRIVATE /std:c++latest)
  add_test(NAME rio_multi_client_smoke COMMAND rio_multi_client_smoke)

  add_executable(rio_capacity_smoke tests/rio_capacity_smoke.cpp)
  target_link_libraries(rio_capacity_smoke PRIVATE ycnet)
  target_include_directories(rio_capacity_smoke PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(rio_capacity_smoke PRIVATE /std:c++latest)
  add_test(NAME rio_capacity_smoke COMMAND rio_capacity_smoke)

  add_executable(rio_timeout_reuse_smoke tests/rio_timeout_reuse_smoke.cpp)
  target_link_libraries(rio_timeout_reuse_smoke PRIVATE ycnet)
  target_include_directories(rio_timeout_reuse_smoke PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(rio_timeout_reuse_smoke PRIVATE /std:c++latest)
  add_test(NAME rio_timeout_reuse_smoke COMMAND rio_timeout_reuse_smoke)

  add_executable(metrics_tests tests/metrics_tests.cpp)
  target_link_libraries(metrics_tests PRIVATE ycnet)
  target_include_directories(metrics_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(metrics_tests PRIVATE /std:c++latest)
  add_test(NAME metrics_tests COMMAND metrics_tests)

  add_executable(unreliable_tests tests/unreliable_tests.cpp)
  target_link_libraries(unreliable_tests PRIVATE ycnet)
  target_compile_options(unreliable_tests PRIVATE /std:c++latest)
  add_test(NAME unreliable_tests COMMAND unreliable_tests)

  add_executable(benchmark_tests tests/benchmark_tests.cpp)
  target_link_libraries(benchmark_tests PRIVATE ycnet)
  target_include_directories(benchmark_tests PRIVATE ${PROJECT_SOURCE_DIR}/benchmarks/include ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(benchmark_tests PRIVATE /std:c++latest)
  add_test(NAME benchmark_tests COMMAND benchmark_tests)

  set_tests_properties(
    rudp_tests
    sequence_reuse_tests
    reliability_policy_tests
    rio_smoke
    ack_loss_tests
    rio_ack_loss_smoke
    send_window_tests
    network_loss_tests
    rio_multi_client_smoke
    rio_capacity_smoke
    rio_timeout_reuse_smoke
    metrics_tests
    unreliable_tests
    benchmark_tests
    PROPERTIES TIMEOUT 30)
endif()

add_executable(loss_benchmark benchmarks/loss_benchmark_main.cpp)
target_link_libraries(loss_benchmark PRIVATE ycnet)
target_include_directories(loss_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/benchmarks/include)
target_compile_options(loss_benchmark PRIVATE /std:c++latest)

if(ycnet_build_comparisons)
  add_executable(protocol_comparison benchmarks/protocol_comparison.cpp)
  target_link_libraries(protocol_comparison PRIVATE ycnet kcp_baseline)
  target_compile_definitions(protocol_comparison PRIVATE
    KCP_BASELINE_COMMIT="b1a7a2101dcbb96017681a500d6b82bbe5a88766")
  if(ycnet_build_tests)
    add_test(NAME protocol_comparison_check COMMAND protocol_comparison --check)
    set_tests_properties(protocol_comparison_check PROPERTIES TIMEOUT 30)
  endif()
endif()
