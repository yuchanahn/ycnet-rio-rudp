cmake_minimum_required(VERSION 3.24)
project(ycnet_rio_rudp LANGUAGES CXX)

add_library(ycnet INTERFACE)
target_include_directories(ycnet INTERFACE ${PROJECT_SOURCE_DIR}/engine/include)
target_compile_features(ycnet INTERFACE cxx_std_23)

option(YCNET_BUILD_TESTS "Build YCNet verification executables" ON)
if(YCNET_BUILD_TESTS)
  enable_testing()

  add_executable(rudp_tests tests/rudp_tests.cpp)
  target_link_libraries(rudp_tests PRIVATE ycnet)
  target_compile_options(rudp_tests PRIVATE /std:c++latest)
  add_test(NAME rudp_tests COMMAND rudp_tests)

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

  add_executable(metrics_tests tests/metrics_tests.cpp)
  target_link_libraries(metrics_tests PRIVATE ycnet)
  target_include_directories(metrics_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(metrics_tests PRIVATE /std:c++latest)
  add_test(NAME metrics_tests COMMAND metrics_tests)

  add_executable(benchmark_tests tests/benchmark_tests.cpp)
  target_link_libraries(benchmark_tests PRIVATE ycnet)
  target_include_directories(benchmark_tests PRIVATE ${PROJECT_SOURCE_DIR}/benchmarks/include ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(benchmark_tests PRIVATE /std:c++latest)
  add_test(NAME benchmark_tests COMMAND benchmark_tests)

  set_tests_properties(
    rudp_tests
    rio_smoke
    ack_loss_tests
    rio_ack_loss_smoke
    send_window_tests
    network_loss_tests
    rio_multi_client_smoke
    rio_capacity_smoke
    metrics_tests
    benchmark_tests
    PROPERTIES TIMEOUT 30)
endif()

add_executable(loss_benchmark benchmarks/loss_benchmark_main.cpp)
target_link_libraries(loss_benchmark PRIVATE ycnet)
target_include_directories(loss_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/benchmarks/include)
target_compile_options(loss_benchmark PRIVATE /std:c++latest)
