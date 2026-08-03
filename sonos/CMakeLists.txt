cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main lwip)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sonos)
include(elf_loader)
idf_build_set_property(
    COMPILE_INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_LIST_DIR}/../sdk/include"
    APPEND
)
project_so(sonos)
