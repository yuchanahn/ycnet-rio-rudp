cmake_minimum_required(VERSION 3.24)
project(ycnet_rio_rudp LANGUAGES CXX)

add_library(ycnet INTERFACE)
target_include_directories(ycnet INTERFACE ${PROJECT_SOURCE_DIR}/engine/include)
target_compile_features(ycnet INTERFACE cxx_std_23)

option(YCNET_BUILD_TESTS "Build YCNet verification executables" ON)
if(YCNET_BUILD_TESTS)
  add_executable(rudp_tests tests/rudp_tests.cpp)
  target_link_libraries(rudp_tests PRIVATE ycnet)
  target_compile_options(rudp_tests PRIVATE /std:c++latest)

  add_executable(rio_smoke tests/rio_smoke.cpp)
  target_link_libraries(rio_smoke PRIVATE ycnet)
  target_include_directories(rio_smoke PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(rio_smoke PRIVATE /std:c++latest)

  add_executable(ack_loss_tests tests/ack_loss_tests.cpp)
  target_link_libraries(ack_loss_tests PRIVATE ycnet)
  target_include_directories(ack_loss_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(ack_loss_tests PRIVATE /std:c++latest)

  add_executable(rio_ack_loss_smoke tests/rio_ack_loss_smoke.cpp)
  target_link_libraries(rio_ack_loss_smoke PRIVATE ycnet)
  target_include_directories(rio_ack_loss_smoke PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(rio_ack_loss_smoke PRIVATE /std:c++latest)

  add_executable(send_window_tests tests/send_window_tests.cpp)
  target_link_libraries(send_window_tests PRIVATE ycnet)
  target_include_directories(send_window_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(send_window_tests PRIVATE /std:c++latest)

  add_executable(network_loss_tests tests/network_loss_tests.cpp)
  target_link_libraries(network_loss_tests PRIVATE ycnet)
  target_include_directories(network_loss_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(network_loss_tests PRIVATE /std:c++latest)

  add_executable(rio_multi_client_smoke tests/rio_multi_client_smoke.cpp)
  target_link_libraries(rio_multi_client_smoke PRIVATE ycnet)
  target_include_directories(rio_multi_client_smoke PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(rio_multi_client_smoke PRIVATE /std:c++latest)
endif()
