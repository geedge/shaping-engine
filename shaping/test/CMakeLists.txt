
###############################################################################
# gtest_shaper_maat
###############################################################################

add_executable(gtest_shaper_maat gtest_shaper_maat.cpp stub.cpp dummy_swarmkv.cpp)
target_include_directories(gtest_shaper_maat PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_include_directories(gtest_shaper_maat PUBLIC ${CMAKE_SOURCE_DIR}/shaping/include)
target_link_libraries(gtest_shaper_maat common shaper pthread gtest)

###############################################################################
# gtest_shaper_maat
###############################################################################

add_executable(gtest_shaper_send_log gtest_shaper_send_log.cpp stub.cpp dummy_swarmkv.cpp)
target_include_directories(gtest_shaper_send_log PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_include_directories(gtest_shaper_send_log PUBLIC ${CMAKE_SOURCE_DIR}/shaping/include)
target_link_libraries(gtest_shaper_send_log common shaper pthread gtest)

###############################################################################
# gtest_shaper
###############################################################################

add_executable(gtest_shaper gtest_shaper.cpp stub.cpp dummy_swarmkv.cpp dummy_time.cpp)
target_include_directories(gtest_shaper PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_include_directories(gtest_shaper PUBLIC ${CMAKE_SOURCE_DIR}/shaping/include)
target_link_libraries(gtest_shaper common shaper pthread gtest)

###############################################################################
# gtest_shaper_aqm
###############################################################################
add_executable(gtest_shaper_aqm gtest_shaper_aqm.cpp)
target_include_directories(gtest_shaper_aqm PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_include_directories(gtest_shaper_aqm PUBLIC ${CMAKE_SOURCE_DIR}/shaping/include)
target_link_libraries(gtest_shaper_aqm common shaper pthread gtest)

###############################################################################
# gtest_discover_tests
###############################################################################

include(GoogleTest)
gtest_discover_tests(gtest_shaper_maat)
gtest_discover_tests(gtest_shaper_send_log)
gtest_discover_tests(gtest_shaper)
gtest_discover_tests(gtest_shaper_aqm)

file(COPY ./test_conf/ DESTINATION ./conf/)