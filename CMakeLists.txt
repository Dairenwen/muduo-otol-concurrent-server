cmake_minimum_required(VERSION 3.16)
project(muduo-otol-concurrent-server VERSION 0.1.0 LANGUAGES CXX)

# epoll、eventfd、timerfd 需要 Linux。
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This server requires Linux; use a Linux environment or container.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
find_package(Threads REQUIRED)
include(CTest)
set(RELEASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/release")

file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/*.cpp")
# main 只属于可执行程序，不放入库。
list(REMOVE_ITEM SERVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/main.cpp")

# 编译一份位置无关的对象，同时生成静态库和动态库。
add_library(server_objects OBJECT ${SERVER_SOURCES})
set_target_properties(server_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(server_objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/hpp")
target_link_libraries(server_objects PRIVATE Threads::Threads)
add_library(server STATIC $<TARGET_OBJECTS:server_objects>)
add_library(server_shared SHARED $<TARGET_OBJECTS:server_objects>)
foreach(target IN ITEMS server server_shared)
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/hpp")
    target_link_libraries(${target} PUBLIC Threads::Threads)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME server
        ARCHIVE_OUTPUT_DIRECTORY "${RELEASE_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${RELEASE_DIR}/lib")
endforeach()
set_target_properties(server_shared PROPERTIES
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

add_executable(server_main "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/main.cpp")
target_link_libraries(server_main PRIVATE server)
set_target_properties(server_main PROPERTIES
    OUTPUT_NAME server RUNTIME_OUTPUT_DIRECTORY "${RELEASE_DIR}")

if(BUILD_TESTING)
    add_executable(test_runner "${CMAKE_CURRENT_SOURCE_DIR}/test/test.cpp")
    target_link_libraries(test_runner PRIVATE server)
    target_compile_definitions(test_runner PRIVATE
        HTTP_TEST_WWWROOT="${CMAKE_CURRENT_SOURCE_DIR}/wwwroot")
    # 原有测试在 assert 中执行操作，Release 构建也必须保留断言。
    target_compile_options(test_runner PRIVATE -UNDEBUG)
    set_target_properties(test_runner PROPERTIES OUTPUT_NAME test)
    add_test(NAME test COMMAND test_runner)
    set_tests_properties(test PROPERTIES TIMEOUT 60
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# 显式运行 cmake --install 才同步发布资源；不生成打包缓存或 CMake 导入配置。
install(TARGETS server server_shared server_main
    RUNTIME DESTINATION . LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/hpp/"
    DESTINATION include/server FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/wwwroot/" DESTINATION wwwroot)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/release/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION .)
