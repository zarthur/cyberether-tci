cmake_minimum_required(VERSION 3.16)
project(cyberether_tci_iq_protocol LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
add_library(tci_iq_protocol
    src/tci/protocol.cpp
    src/tci/client.cpp
)
target_include_directories(tci_iq_protocol PUBLIC include)
target_link_libraries(tci_iq_protocol PUBLIC Threads::Threads)

enable_testing()
add_executable(tci_iq_protocol_test tests/protocol_test.cpp)
target_link_libraries(tci_iq_protocol_test PRIVATE tci_iq_protocol)
add_test(NAME tci_iq_protocol_synthetic COMMAND tci_iq_protocol_test)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(tci_iq_client_test tests/client_fake_test.cpp)
target_link_libraries(tci_iq_client_test PRIVATE tci_iq_protocol)
add_test(NAME tci_iq_websocket_fake_server
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/fake_tci_server.py $<TARGET_FILE:tci_iq_client_test>)
set_tests_properties(tci_iq_websocket_fake_server PROPERTIES TIMEOUT 15)
