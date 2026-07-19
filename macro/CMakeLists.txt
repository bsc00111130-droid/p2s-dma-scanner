cmake_minimum_required(VERSION 3.20)
project(dma_reader VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_definitions(-D_WIN32_WINNT=0x0A00)

add_library(dmareader SHARED
    reader.h
    ptr_chain.h
)

target_compile_options(dmareader PRIVATE /O2 /GL /arch:AVX2 /GS- /Gy)
target_link_options(dmareader PRIVATE /LTCG /OPT:REF /OPT:ICF)

if(BUILD_TEST)
    add_executable(dma_test main.cpp)
    target_link_libraries(dma_test dmareader)
endif()
