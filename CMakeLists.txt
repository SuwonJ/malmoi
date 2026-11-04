cmake_minimum_required(VERSION 3.20)
project(malmoi LANGUAGES C)

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
