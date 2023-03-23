if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CPACK_PACKAGE_NAME "shaping_engine-debug")
else()
    set(CPACK_PACKAGE_NAME "shaping_engine")
endif()

message(STATUS "Package: ${CPACK_PACKAGE_NAME}")

set(CPACK_PACKAGE_VENDOR "MESASOFT")
set(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VERSION_PATCH}.${DESCRIBE}")
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})

# RPM Build
set(CPACK_GENERATOR "RPM")
set(CPACK_RPM_AUTO_GENERATED_FILE_NAME ON)
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
set(CPACK_RPM_PACKAGE_AUTOREQPROV "no")
set(CPACK_RPM_PACKAGE_RELEASE_DIST on)
set(CPACK_RPM_DEBUGINFO_PACKAGE on)
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE ${CMAKE_SOURCE_DIR}/cmake/PostInstall.in)
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE ${CMAKE_SOURCE_DIR}/cmake/PostUninstall.in)
set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE ${CMAKE_SOURCE_DIR}/cmake/PreUninstall.in)

# Must uninstall the debug package before install release package
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CPACK_RPM_PACKAGE_CONFLICTS "shaping_engine")
else()
    set(CPACK_RPM_PACKAGE_CONFLICTS "shaping_engine-debug")
endif()

# setup %config(noreplace)
set(CPACK_RPM_USER_FILELIST "%config(noreplace) ${CMAKE_INSTALL_PREFIX}/conf/main.conf"
                            "%config(noreplace) ${CMAKE_INSTALL_PREFIX}/conf/zlog.conf"
                            "%config(noreplace) ${CMAKE_INSTALL_PREFIX}/conf/table_info.json")

include(CPack)
