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
add_test(NAME tci_iq_protocol_checks_survive_ndebug
    COMMAND tci_iq_protocol_test --verify-check-failure)
set_tests_properties(tci_iq_protocol_checks_survive_ndebug PROPERTIES WILL_FAIL TRUE)

add_executable(tci_iq_disconnect_send_test tests/disconnect_during_send_test.cpp)
target_include_directories(tci_iq_disconnect_send_test PRIVATE src)
add_test(NAME tci_iq_disconnect_during_send COMMAND tci_iq_disconnect_send_test)

add_executable(tci_iq_connect_cancel_test tests/connect_cancel_test.cpp)
target_link_libraries(tci_iq_connect_cancel_test PRIVATE tci_iq_protocol)
add_test(NAME tci_iq_connect_cancel COMMAND tci_iq_connect_cancel_test)
set_tests_properties(tci_iq_connect_cancel PROPERTIES TIMEOUT 5)

add_executable(tci_iq_connect_deadline_test tests/connect_deadline_test.cpp)
target_include_directories(tci_iq_connect_deadline_test PRIVATE src)
add_test(NAME tci_iq_connect_deadline COMMAND tci_iq_connect_deadline_test)

add_executable(tci_iq_block_contract_test tests/block_contract_test.cpp)
target_include_directories(tci_iq_block_contract_test PRIVATE tests/fake_jetstream include src)
target_link_libraries(tci_iq_block_contract_test PRIVATE tci_iq_protocol)
add_test(NAME tci_iq_block_contract COMMAND tci_iq_block_contract_test)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME tci_iq_cep_packaging
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/package_cep_test.py)
add_executable(tci_iq_client_test tests/client_fake_test.cpp)
target_link_libraries(tci_iq_client_test PRIVATE tci_iq_protocol)
add_test(NAME tci_iq_websocket_fake_server
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/fake_tci_server.py $<TARGET_FILE:tci_iq_client_test>)
set_tests_properties(tci_iq_websocket_fake_server PROPERTIES TIMEOUT 15)
