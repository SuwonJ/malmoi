cmake_minimum_required(VERSION 3.20)
project(malmoi LANGUAGES C)
option(MALMOI_WITH_NATIVE_INDEX "Enable full-corpus native Kiwi+TF-IDF index" ON)

# Full dictionary is versioned in Git-sized parts in this same repository.
# CMake reconstructs it; teammates need neither Python nor Git LFS setup.
if(NOT MALMOI_WITH_NATIVE_INDEX AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/full/manifest.cmake")
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
    src/bridge.c
)
target_include_directories(malmoi_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
if(NOT WIN32)
    target_link_libraries(malmoi_core PUBLIC m)
endif()

if(MALMOI_WITH_NATIVE_INDEX)
    set(KIWI_ROOT "" CACHE PATH "Kiwi 0.24.0 native distribution")
    include(cmake/NativeRuntime.cmake)
    find_path(KIWI_INCLUDE_DIR kiwi/capi.h HINTS "${KIWI_ROOT}/include" REQUIRED)
    find_library(KIWI_LIBRARY NAMES kiwi HINTS "${KIWI_ROOT}/lib" REQUIRED)
    find_package(Threads REQUIRED)
    target_sources(malmoi_core PRIVATE src/internal/native_lexical.c src/internal/sha256.c)
    target_include_directories(malmoi_core PRIVATE "${KIWI_INCLUDE_DIR}")
    target_compile_definitions(malmoi_core PRIVATE MALMOI_WITH_NATIVE_INDEX)
    target_link_libraries(malmoi_core PUBLIC "${KIWI_LIBRARY}" Threads::Threads)
    if(APPLE)
        set(CMAKE_BUILD_RPATH "@loader_path/runtime/kiwi/lib")
    elseif(UNIX)
        set(CMAKE_BUILD_RPATH "$ORIGIN/runtime/kiwi/lib")
    endif()
    # Both executables find runtime/ beside themselves, including multi-config builds.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}")
    endforeach()
    if(WIN32)
        file(GLOB kiwi_dlls "${KIWI_ROOT}/bin/*.dll" "${KIWI_ROOT}/lib/*.dll")
        file(COPY ${kiwi_dlls} DESTINATION "${CMAKE_BINARY_DIR}")
    endif()
endif()

# Main team CLI (independent of the standalone games).
add_executable(malmoi
    src/main.c
    games/mallangi/game.c
    games/mallangi/similarity.c
)
target_include_directories(malmoi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/games/mallangi"
)
target_link_libraries(malmoi PRIVATE malmoi_core)

# Kim Sihyeon's offline two-player word similarity game.
# Keep its main() separate; share the same public malmoi_core library.
add_executable(wordgame_sii
    gameSH/main.c
    gameSH/game.c
    gameSH/word_check.c
    gameSH/ui.c
)
target_include_directories(wordgame_sii PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/gameSH"
)
target_link_libraries(wordgame_sii PRIVATE malmoi_core)

# Team bundles carry the full dataset. Copy it beside both executables so
# launches from CLion, Explorer or an unrelated working directory all work.
if(NOT MALMOI_WITH_NATIVE_INDEX AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/full.mlm")
    foreach(_app IN ITEMS malmoi wordgame_sii)
        add_custom_command(TARGET ${_app} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_app}>/data"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/full.mlm"
                "$<TARGET_FILE_DIR:${_app}>/data/dictionary.mlm")
    endforeach()
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

if(MALMOI_WITH_NATIVE_INDEX)
    add_executable(test_native_index_live EXCLUDE_FROM_ALL tests/test_native_index_live.c)
    target_link_libraries(test_native_index_live PRIVATE malmoi_core)
    add_executable(test_unicode_lower EXCLUDE_FROM_ALL tests/test_unicode_lower.c)
endif()

if(MALMOI_WITH_NATIVE_INDEX)
    message(STATUS "Default reverse backend: native-index (TF-IDF + Kiwi BM25)")
else()
    message(WARNING "MALMOI_WITH_NATIVE_INDEX=OFF: this build uses legacy reverse search. Use cmake --preset team for full-corpus search; existing cache options persist.")
endif()

if(MALMOI_WITH_NATIVE_INDEX)
    add_executable(test_native_all_api EXCLUDE_FROM_ALL tests/test_native_all_api.c games/mallangi/game.c games/mallangi/similarity.c)
    target_include_directories(test_native_all_api PRIVATE games/mallangi)
    target_link_libraries(test_native_all_api PRIVATE malmoi_core)
endif()
if(MALMOI_WITH_NATIVE_INDEX)
    add_executable(test_native_two_player EXCLUDE_FROM_ALL tests/test_native_two_player.c gameSH/game.c gameSH/word_check.c)
    target_include_directories(test_native_two_player PRIVATE gameSH)
    target_link_libraries(test_native_two_player PRIVATE malmoi_core)
endif()

include(CTest)
if(BUILD_TESTING AND MALMOI_WITH_NATIVE_INDEX)
    add_test(NAME native_all_api COMMAND test_native_all_api)
    add_test(NAME native_two_player COMMAND test_native_two_player)
    add_test(NAME native_unicode COMMAND test_unicode_lower)
    set_tests_properties(native_all_api native_two_player PROPERTIES ENVIRONMENT "MALMOI_DATA=does-not-exist.mlm" RUN_SERIAL TRUE)
endif()

# Optional existing local web bridge; the C search engine itself needs no Python.
if(UNIX)
    set_target_properties(malmoi_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(mallangi_web SHARED EXCLUDE_FROM_ALL games/mallangi/bridge.c games/mallangi/game.c games/mallangi/similarity.c games/mallangi/daily.c)
    target_include_directories(mallangi_web PRIVATE games/mallangi)
    target_link_libraries(mallangi_web PRIVATE malmoi_core)
endif()
if(MALMOI_WITH_NATIVE_INDEX)
    add_executable(test_native_sense_game EXCLUDE_FROM_ALL tests/test_native_sense_game.c games/mallangi/game.c games/mallangi/similarity.c)
    target_include_directories(test_native_sense_game PRIVATE games/mallangi)
    target_link_libraries(test_native_sense_game PRIVATE malmoi_core)
    if(BUILD_TESTING)
        add_test(NAME native_sense_game COMMAND test_native_sense_game)
        set_tests_properties(native_sense_game PROPERTIES ENVIRONMENT "MALMOI_DATA=does-not-exist.mlm" RUN_SERIAL TRUE)
    endif()
endif()
if(BUILD_TESTING AND MALMOI_WITH_NATIVE_INDEX)
    add_executable(test_native_hybrid_math EXCLUDE_FROM_ALL tests/test_native_hybrid_math.c)
    add_test(NAME native_hybrid_math COMMAND test_native_hybrid_math)
endif()
