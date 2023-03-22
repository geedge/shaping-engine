cmake_minimum_required(VERSION 3.0.0)

add_definitions(-fPIC)
add_library(avl_tree libavl.c avl_tree.c)
target_include_directories(avl_tree PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../include)