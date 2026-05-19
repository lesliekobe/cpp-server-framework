cmake_minimum_required(VERSION 3.20)
project(cpp_server_framework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# Platform detection
if(WIN32)
    set(PLATFORM_WINDOWS ON)
    set(PLATFORM_LINUX OFF)
    add_definitions(-DPLATFORM_WINDOWS=1 -DPLATFORM_LINUX=0)
else()
    set(PLATFORM_WINDOWS OFF)
    set(PLATFORM_LINUX ON)
    add_definitions(-DPLATFORM_WINDOWS=0 -DPLATFORM_LINUX=1)
endif()

# Source groups
set(BASE_SOURCES
    base/logger/logger.h base/logger/logger.cpp
    base/time/timestamp.h base/time/timestamp.cpp
    base/lock/mutex.h base/lock/mutex.cpp
    base/lock/condition.h base/lock/condition.cpp
    base/memory/memory_pool.h base/memory/memory_pool.cpp
    base/event/event_loop.h base/event/event_loop.cpp
    base/timer/timer.h base/timer/timer.cpp
    base/message_queue/message_queue.h base/message_queue/message_queue.cpp
    base/config/config.h base/config/config.cpp
    base/object_pool/object_pool.h base/object_pool/object_pool.cpp
    base/process/signal_handler.h base/process/signal_handler.cpp
)

set(CORE_SOURCES
    core/threadpool/thread_pool.h core/threadpool/thread_pool.cpp
    core/task/task.h core/task/task.cpp
    core/monitor/monitor.h core/monitor/monitor.cpp
)

set(NETWORK_SOURCES
    network/tcp/tcp_server.h network/tcp/tcp_server.cpp
    network/tcp/tcp_client.h network/tcp/tcp_client.cpp
    network/tcp/tcp_connection.h network/tcp/tcp_connection.cpp
    network/protocol/packet.h network/protocol/packet.cpp
    network/codec/codec.h network/codec/codec.cpp
    network/codec/crc.h
)

set(BUSINESS_SOURCES
    business/session/session_mgr.h business/session/session_mgr.cpp
    business/router/router.h business/router/router.cpp
)

set(DB_SOURCES
    db/connection_pool.h db/connection_pool.cpp
    db/query_task.h
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
    ${BUSINESS_SOURCES} ${DB_SOURCES} ${ADAPTER_SOURCES}
)
target_include_directories(framework PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Windows: link ws2_32
if(WIN32)
    target_link_libraries(framework ws2_32)
endif()

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
add_test(NAME tcp_test COMMAND tcp_test)

# Print summary
message(STATUS "=== C++ Server Framework ===")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Source files: ${CMAKE_CURRENT_SOURCE_DIR}")