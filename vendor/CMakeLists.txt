include(ExternalProject)

# GoogleTest
ExternalProject_Add(googletest PREFIX googletest
        URL ${CMAKE_CURRENT_SOURCE_DIR}/googletest-release-1.8.0.tar.gz
        URL_MD5 16877098823401d1bf2ed7891d7dce36
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

ExternalProject_Get_Property(googletest INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)

add_library(gtest STATIC IMPORTED GLOBAL)
add_dependencies(gtest googletest)
set_property(TARGET gtest PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libgtest.a)
set_property(TARGET gtest PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
set_property(TARGET gtest PROPERTY INTERFACE_LINK_LIBRARIES pthread)

add_library(gmock STATIC IMPORTED GLOBAL)
add_dependencies(gmock googletest)
set_property(TARGET gmock PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libgmock.a)
set_property(TARGET gmock PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)

# cjson
ExternalProject_Add(cJSON PREFIX cJSON
        URL ${CMAKE_CURRENT_SOURCE_DIR}/cJSON-1.7.7.tar.gz
        URL_MD5 715009c99728bf81d6c97352718650ff
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_AND_STATIC_LIBS=1)

ExternalProject_Get_Property(cJSON INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)

add_library(cjson SHARED IMPORTED GLOBAL)
add_dependencies(cjson cJSON)
set_property(TARGET cjson PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib64/libcjson.a)
set_property(TARGET cjson PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)

# jemalloc
ExternalProject_Add(libjemalloc PREFIX libjemalloc
        URL ${CMAKE_CURRENT_SOURCE_DIR}/jemalloc-5.3.0.tar.bz2
        CONFIGURE_COMMAND ./autogen.sh --prefix=<INSTALL_DIR> --enable-prof
        BUILD_COMMAND make
		BUILD_IN_SOURCE 1
        INSTALL_COMMAND make install prefix=<INSTALL_DIR>)

ExternalProject_Get_Property(libjemalloc INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)

add_library(libjemalloc-static STATIC IMPORTED GLOBAL)
add_dependencies(libjemalloc-static libjemalloc)
set_property(TARGET libjemalloc-static PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libjemalloc.a)
set_property(TARGET libjemalloc-static PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
