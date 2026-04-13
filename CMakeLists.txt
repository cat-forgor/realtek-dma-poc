cmake_minimum_required(VERSION 3.24)
project(realtek_poc_injector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /Oi /GL /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /INCREMENTAL:NO")
endif()

add_executable(inject
    src/main.cpp
    src/registry.cpp
    src/exploit.cpp
)

target_include_directories(inject PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(inject PRIVATE advapi32 dbghelp)

set_target_properties(inject PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
