cmake_minimum_required(VERSION 3.20)
project(cpp_server_framework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# Platform detection
if(WIN32)
    set(PLATFORM_WINDOWS ON)
    set(PLATFORM_LINUX OFF)
else()
    set(PLATFORM_WINDOWS OFF)
    set(PLATFORM_LINUX ON)
endif()

# Source groups
set(BASE_SOURCES
    base/logger/logger.h base/logger/logger.cpp
    base/time/timestamp.h base/time/timestamp.cpp
    base/lock/mutex.h base/lock/mutex.cpp
    base/lock/condition.h base/lock/condition.cpp
    base/memory/memory_pool.h base/memory/memory_pool.cpp
)

set(CORE_SOURCES
    core/threadpool/thread_pool.h core/threadpool/thread_pool.cpp
    core/task/task.h core/task/task.cpp
)

set(NETWORK_SOURCES
    network/tcp/tcp_server.h network/tcp/tcp_server.cpp
    network/tcp/tcp_client.h network/tcp/tcp_client.cpp
    network/tcp/tcp_connection.h network/tcp/tcp_connection.cpp
    network/protocol/packet.h network/protocol/packet.cpp
)

set(BUSINESS_SOURCES
    business/session/session_mgr.h business/session/session_mgr.cpp
    business/router/router.h business/router/router.cpp
)

set(ADAPTER_SOURCES
    adapter/platform.h adapter/platform.cpp
)

set(EXAMPLE_SOURCES
    example/server/main.cpp
    example/gateway/gateway.cpp
)

# Library
add_library(framework STATIC
    ${BASE_SOURCES} ${CORE_SOURCES} ${NETWORK_SOURCES}
    ${BUSINESS_SOURCES} ${ADAPTER_SOURCES}
)
target_include_directories(framework PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Examples
add_executable(tcp_server_example example/server/main.cpp)
target_link_libraries(tcp_server_example framework)

add_executable(gateway_example example/gateway/gateway.cpp)
target_link_libraries(gateway_example framework)

# Tests
enable_testing()
add_executable(threadpool_test test/threadpool_test.cpp)
target_link_libraries(threadpool_test framework)

add_executable(logger_test test/logger_test.cpp)
target_link_libraries(logger_test framework)

add_executable(tcp_test test/tcp_test.cpp)
target_link_libraries(tcp_test framework)

add_test(NAME threadpool_test COMMAND threadpool_test)
add_test(NAME logger_test COMMAND logger_test)