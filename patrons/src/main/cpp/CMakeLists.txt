cmake_minimum_required(VERSION 3.10.2)

project(patrons)

add_definitions("
    -O3
")

add_library(patrons SHARED
        patrons_core.c

        xhook/xh_core.c
        xhook/xh_elf.c
        xhook/xh_log.c
        xhook/xh_util.c
        xhook/xh_version.c
        xhook/xhook.c
        )

find_library(log-lib log)

target_link_libraries(patrons
        ${log-lib}
        z
        )