cmake_minimum_required(VERSION 3.10.2)

project("memory-alloc")

find_library(log-lib log)

add_library(memory-alloc SHARED
        mem_test.cpp
        )

target_link_libraries(
        memory-alloc
        ${log-lib}
)