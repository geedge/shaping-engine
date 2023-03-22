###############################################################################
# gtest_addr_tuple4
###############################################################################

add_executable(gtest_addr_tuple4 gtest_addr_tuple4.cpp)
target_include_directories(gtest_addr_tuple4 PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_link_libraries(gtest_addr_tuple4 common pthread gtest)

###############################################################################
# gtest_session_table
###############################################################################

add_executable(gtest_session_table gtest_session_table.cpp)
target_include_directories(gtest_session_table PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_link_libraries(gtest_session_table common pthread gtest)

###############################################################################
# gtest_raw_packet
###############################################################################

add_executable(gtest_raw_packet gtest_raw_packet.cpp)
target_include_directories(gtest_raw_packet PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_link_libraries(gtest_raw_packet common pthread gtest)

###############################################################################
# gtest_ctrl_packet
###############################################################################

add_executable(gtest_ctrl_packet gtest_ctrl_packet.cpp)
target_include_directories(gtest_ctrl_packet PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_include_directories(gtest_ctrl_packet PUBLIC ${CMAKE_SOURCE_DIR}/shaping/include)
target_link_libraries(gtest_ctrl_packet common shaper mrzcpd pthread gtest)

###############################################################################
# gtest_tcp_pure_ctrl_pkt
###############################################################################
add_executable(gtest_tcp_pure_ctrl_pkt gtest_tcp_pure_ctrl_pkt.cpp)
target_include_directories(gtest_tcp_pure_ctrl_pkt PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_include_directories(gtest_tcp_pure_ctrl_pkt PUBLIC ${CMAKE_SOURCE_DIR}/shaping/include)
target_link_libraries(gtest_tcp_pure_ctrl_pkt common pthread gtest)

###############################################################################
# gtest_utils
###############################################################################

add_executable(gtest_utils gtest_utils.cpp)
target_include_directories(gtest_utils PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_link_libraries(gtest_utils common pthread gtest)

###############################################################################
# gtest_utils
###############################################################################

add_executable(gtest_libavl gtest_libavl.cpp)
target_include_directories(gtest_libavl PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_link_libraries(gtest_libavl common avl_tree pthread gtest)

###############################################################################
# gtest_discover_tests
###############################################################################

include(GoogleTest)
gtest_discover_tests(gtest_addr_tuple4)
gtest_discover_tests(gtest_session_table)
gtest_discover_tests(gtest_raw_packet)
gtest_discover_tests(gtest_ctrl_packet)
gtest_discover_tests(gtest_utils)
gtest_discover_tests(gtest_libavl)
