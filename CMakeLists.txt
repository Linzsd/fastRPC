cmake_minimum_required(VERSION 3.28)
project(fastRPC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -pthread")

include_directories(
        libcoroutine
)

set(SRC libcoroutine/coroutine.cpp
        libcoroutine/thread.cpp
        libcoroutine/scheduler.cpp
        libcoroutine/ioscheduler.cpp
        libcoroutine/timer.cpp
)

add_library(fastRPC SHARED ${SRC})

set(LIBS fastRPC pthread)

# 测试
add_executable(test_coroutine test/test_coroutine.cpp)
target_link_libraries(test_coroutine ${LIBS})


#设置输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH  ${PROJECT_SOURCE_DIR}/lib)