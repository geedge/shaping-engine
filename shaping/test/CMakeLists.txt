
###############################################################################
# gtest_shaper_maat
###############################################################################

add_executable(gtest_shaper_maat gtest_shaper_maat.cpp stub.cpp)
target_include_directories(gtest_shaper_maat PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_include_directories(gtest_shaper_maat PUBLIC ${CMAKE_SOURCE_DIR}/shaping/include)
target_link_libraries(gtest_shaper_maat common shaper pthread gtest)

###############################################################################
# gtest_shaper
###############################################################################

add_executable(gtest_shaper gtest_shaper.cpp stub.cpp)
target_include_directories(gtest_shaper PUBLIC ${CMAKE_SOURCE_DIR}/common/include)
target_include_directories(gtest_shaper PUBLIC ${CMAKE_SOURCE_DIR}/shaping/include)
target_link_libraries(gtest_shaper common shaper pthread gtest)

###############################################################################
# gtest_discover_tests
###############################################################################

include(GoogleTest)
gtest_discover_tests(gtest_shaper_maat)
