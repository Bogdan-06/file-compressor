cmake_minimum_required(VERSION 3.20)

project(FileCompressor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(FileCompressor WIN32
    src/main.cpp
)

target_compile_definitions(FileCompressor PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(FileCompressor PRIVATE
    comdlg32
    comctl32
    ole32
    shell32
    uuid
)

if(MINGW)
    target_link_options(FileCompressor PRIVATE
        -municode
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()
