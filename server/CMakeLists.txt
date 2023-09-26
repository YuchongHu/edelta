cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)

# include third party library
set(SOCKPP_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(SOCKPP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/leveldb EXCLUDE_FROM_ALL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/sockpp)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/delta)
set(thread_pool_lib_include ${CMAKE_CURRENT_SOURCE_DIR}/lib/thread-pool) # the thread-pool lib is header-only

# ser libs
set(libs -lcrypto -lssl -lboost_system -lboost_iostreams -lrt -lstdc++fs)

add_executable(server main.cpp)
target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${thread_pool_lib_include})
target_link_libraries(server leveldb sockpp-static delta ${libs})
