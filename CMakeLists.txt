cmake_minimum_required(VERSION 3.16)

project(muduo-otol-concurrent-server LANGUAGES CXX)

include(CTest)
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

find_package(Threads REQUIRED)

file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if(SERVER_SOURCES)
    add_executable(serverapp ${SERVER_SOURCES})
    target_include_directories(serverapp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(serverapp PRIVATE Threads::Threads)
else()
    message(STATUS "No source files found under src; skipping serverapp target.")
endif()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp"
)

if(TEST_SOURCES)
    add_executable(testapp ${TEST_SOURCES})
    target_include_directories(testapp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/test"
    )
    target_link_libraries(testapp PRIVATE Threads::Threads)
else()
    message(STATUS "No test source files found under test; skipping testapp target.")
endif()
