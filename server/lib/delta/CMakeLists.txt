cmake_minimum_required(VERSION 3.10)
project(delta)

set(CMAKE_C_STANDARD 99)

set(src ${CMAKE_CURRENT_SOURCE_DIR}/src/rabin.c ${CMAKE_CURRENT_SOURCE_DIR}/src/spook.c ${CMAKE_CURRENT_SOURCE_DIR}/src/delta.c)
set(xdelta_dir ${CMAKE_CURRENT_SOURCE_DIR}/lib/xdelta3)
set(xdelta_src ${xdelta_dir}/xdelta3.c)

add_library(delta STATIC ${src} ${xdelta_src})
target_include_directories(delta PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${xdelta_dir})
