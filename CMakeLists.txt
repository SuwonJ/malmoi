cmake_minimum_required(VERSION 3.20)
project(malmoi LANGUAGES C)

# Full dictionary is versioned in Git-sized parts in this same repository.
# CMake reconstructs it; teammates need neither Python nor Git LFS setup.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/full/manifest.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/data/full/manifest.cmake")
    set(_full_dictionary "${CMAKE_CURRENT_SOURCE_DIR}/data/full.mlm")
    set(_full_hash "")
    if(EXISTS "${_full_dictionary}")
        file(SHA256 "${_full_dictionary}" _full_hash)
    endif()
    if(NOT _full_hash STREQUAL MALMOI_FULL_SHA256)
        execute_process(COMMAND ${CMAKE_COMMAND} -E cat ${MALMOI_FULL_PARTS}
            OUTPUT_FILE "${_full_dictionary}.tmp" RESULT_VARIABLE _assemble_result)
        if(NOT _assemble_result EQUAL 0)
            message(FATAL_ERROR "Full dictionary parts are missing or unreadable")
        endif()
        file(SHA256 "${_full_dictionary}.tmp" _full_hash)
        if(NOT _full_hash STREQUAL MALMOI_FULL_SHA256)
            message(FATAL_ERROR "Full dictionary checksum mismatch")
        endif()
        file(RENAME "${_full_dictionary}.tmp" "${_full_dictionary}")
    endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Shared C engine: independent of the CLI, GUI, and any future server.
add_library(malmoi_core STATIC
    src/malmoi.c
    src/vecdb.c
    src/internal/search_backend.c
    src/internal/lexical.c
)
target_include_directories(malmoi_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
if(NOT WIN32)
    target_link_libraries(malmoi_core PUBLIC m)
endif()

# One default application for CLion. Existing test/demo entry points are not built.
add_executable(malmoi
    src/main.c
    games/mallangi/game.c
    games/mallangi/similarity.c
)
target_include_directories(malmoi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/games/mallangi"
)
target_link_libraries(malmoi PRIVATE malmoi_core)

# Team bundles carry the full dataset. Copy it beside the executable so
# launches from CLion, Explorer or an unrelated working directory all work.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/full.mlm")
    add_custom_command(TARGET malmoi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:malmoi>/data"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/full.mlm"
            "$<TARGET_FILE_DIR:malmoi>/data/dictionary.mlm")
endif()

# Default builds retain the standalone, model-free C API.
option(MALMOI_WITH_E5 "Enable offline E5 through embedded Python/ONNX Runtime" OFF)
if(MALMOI_WITH_E5)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter Development.Embed)
    target_sources(malmoi_core PRIVATE src/internal/e5_encoder.c)
    target_link_libraries(malmoi_core PRIVATE Python3::Python)
    target_compile_definitions(malmoi_core PRIVATE MALMOI_WITH_E5
        MALMOI_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")
endif()

add_executable(malmoi_cli EXCLUDE_FROM_ALL examples/malmoi_cli.c)
target_link_libraries(malmoi_cli PRIVATE malmoi_core)
if(WIN32)
    if(MINGW)
        target_link_options(malmoi_cli PRIVATE -municode)
    endif()
endif()
