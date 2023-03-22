# CMakeFiles for 3rd vendor library

include(ExternalProject)
# libgtest
ExternalProject_Add(libgtest PREFIX libgtest
        URL ${CMAKE_SOURCE_DIR}/vendor/googletest-release-1.10.0.tar.gz
        URL_MD5 ecd1fa65e7de707cd5c00bdac56022cd
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
ExternalProject_Get_Property(libgtest INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)
add_library(gtest-static STATIC IMPORTED GLOBAL)
add_dependencies(gtest-static libgtest)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set_property(TARGET gtest-static PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib64/libgtestd.a)
else()
	set_property(TARGET gtest-static PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib64/libgtest.a)
endif()
set_property(TARGET gtest-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
set_property(TARGET gtest-static PROPERTY INTERFACE_LINK_LIBRARIES pthread)


# MESA Framework
set(MRZCPD_LIB_DIR /opt/mrzcpd/lib)
set(MRZCPD_INCLUDE_DIR /opt/mrzcpd/include)

add_library(mrzcpd SHARED IMPORTED GLOBAL)
set_property(TARGET mrzcpd PROPERTY IMPORTED_LOCATION ${MRZCPD_LIB_DIR}/libmarsio.so)
set_property(TARGET mrzcpd PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${MRZCPD_INCLUDE_DIR})