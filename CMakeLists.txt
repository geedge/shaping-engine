cmake_minimum_required(VERSION 3.5)
project(shaping_engine)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Version)
include(Package)

add_definitions(-D_GNU_SOURCE)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug)
endif()

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set (CMAKE_INSTALL_PREFIX "/opt/tsg/shaping_engine" CACHE PATH "default install path" FORCE)
endif()

# Global Compile Options
option(ENABLE_PIC "Generate position independent code (necessary for shared libraries)" TRUE)
option(ENABLE_WARNING_ALL "Enable all optional warnings which are desirable for normal code" TRUE)

if(NOT ASAN_OPTION)
    option(ENABLE_SANITIZE_ADDRESS "Enable AddressSanitizer" FALSE)
else()
    option(ENABLE_SANITIZE_ADDRESS "Enable AddressSanitizer" TRUE)
endif()

option(ENABLE_SANITIZE_THREAD "Enable ThreadSanitizer" FALSE)

if(ENABLE_PIC)
	set(CMAKE_POSITION_INDEPENDENT_CODE 1)
endif()

if(ENABLE_WARNING_ALL)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()

if(ENABLE_SANITIZE_ADDRESS)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
elseif(ENABLE_SANITIZE_THREAD)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
endif()

if(ENABLE_SANITIZE_ADDRESS AND ENABLE_SANITIZE_THREAD)
	message(WARNING "Both ENABLE_SANITIZE_ADDRESS and ENABLE_SANITIZE_THREAD set, only ENABLE_SANITIZE_ADDRESS effected.")
endif()

add_custom_target("install-program" COMMAND ${CMAKE_COMMAND} ARGS -DCOMPONENT=Program -P cmake_install.cmake)
add_custom_target("install-profile" COMMAND ${CMAKE_COMMAND} ARGS -DCOMPONENT=Profile -P cmake_install.cmake)

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

enable_testing()
add_subdirectory(conf)
add_subdirectory(vendor)
add_subdirectory(common)
add_subdirectory(shaping)
add_subdirectory(script)