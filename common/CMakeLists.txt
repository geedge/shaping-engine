add_library(common src/log.cpp src/session_table.cpp src/utils.cpp src/addr_tuple4.cpp src/raw_packet.cpp)

target_link_libraries(common PUBLIC MESA_handle_logger)
#target_link_libraries(common PUBLIC mrzcpd)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# MESA Framework
set(MESA_FRAMEWORK_LIB_DIR /opt/MESA/lib)
set(MESA_FRAMEWORK_INCLUDE_DIR /opt/MESA/include)

add_library(MESA_handle_logger SHARED IMPORTED GLOBAL)
set_property(TARGET MESA_handle_logger PROPERTY IMPORTED_LOCATION ${MESA_FRAMEWORK_LIB_DIR}/libMESA_handle_logger.so)
set_property(TARGET MESA_handle_logger PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${MESA_FRAMEWORK_INCLUDE_DIR})

add_library(MESA_prof_load SHARED IMPORTED GLOBAL)
set_property(TARGET MESA_prof_load PROPERTY IMPORTED_LOCATION ${MESA_FRAMEWORK_LIB_DIR}/libMESA_prof_load.so)
set_property(TARGET MESA_prof_load PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${MESA_FRAMEWORK_INCLUDE_DIR})

set(MRZCPD_LIB_DIR /opt/mrzcpd/lib)
set(MRZCPD_INCLUDE_DIR /opt/mrzcpd/include)

add_library(mrzcpd SHARED IMPORTED GLOBAL)
set_property(TARGET mrzcpd PROPERTY IMPORTED_LOCATION ${MRZCPD_LIB_DIR}/libmarsio.so)
set_property(TARGET mrzcpd PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${MRZCPD_INCLUDE_DIR})

add_library(breakpad_mini SHARED IMPORTED GLOBAL)
set_property(TARGET breakpad_mini PROPERTY IMPORTED_LOCATION ${MESA_FRAMEWORK_LIB_DIR}/libbreakpad_mini.so)
set_property(TARGET breakpad_mini PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${MESA_FRAMEWORK_INCLUDE_DIR})


add_subdirectory(libavl)
add_subdirectory(test)