cmake_minimum_required(VERSION 3.16)

project(muduo-otol-concurrent-server LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/*.cpp"
)

if(SERVER_SOURCES)
    add_library(server STATIC ${SERVER_SOURCES})
    target_include_directories(server PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hpp"
    )
    target_link_libraries(server PUBLIC Threads::Threads)
else()
    message(FATAL_ERROR "No C++ source files found under src/cpp.")
endif()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp"
)

if(TEST_SOURCES)
    add_executable(test_runner ${TEST_SOURCES})
    set_target_properties(test_runner PROPERTIES OUTPUT_NAME test)
    target_link_libraries(test_runner PRIVATE server)
    add_test(NAME test COMMAND test_runner)
else()
    message(FATAL_ERROR "No C++ test source files found under test.")
endif()
