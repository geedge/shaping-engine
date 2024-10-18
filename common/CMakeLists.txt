add_library(common src/log.cpp src/session_table.cpp src/utils.cpp src/addr_tuple4.cpp src/raw_packet.cpp src/mpack.c)

target_link_libraries(common PUBLIC MESA_handle_logger uuid)
target_link_libraries(common PUBLIC mrzcpd)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

add_subdirectory(libavl)
add_subdirectory(test)