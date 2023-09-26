cmake_minimum_required(VERSION 3.10)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src src_dir)

# add a static library target dedupClientLib
add_library(clientL STATIC ${src_dir} )
target_include_directories(clientL PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(clientL -L/usr/local/ssl/lib    -lssl -lcrypto -lpthread -ldl -lgf_complete)
add_executable(client ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)
target_link_libraries(client clientL)
target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)