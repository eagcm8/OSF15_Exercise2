cmake_minimum_required (VERSION 2.8.0)
project (bs)

set(CMAKE_C_FLAGS "-std=c99 -Wall -Werror -g -O0")


find_library(bitmap_lib bitmap)

add_library(block_store SHARED src/block_store.c)
target_link_libraries(block_store ${bitmap_lib})

install(TARGETS block_store DESTINATION lib)
install(FILES include/block_store.h DESTINATION include)

add_executable(bs_tester test/test.c)
target_link_libraries(bs_tester ${bitmap_lib})

set(CMAKE_CXX_FLAGS "-std=c++11 -Wall -Werror -O3")

add_executable(generate_drive src/generate_drive.cpp)

