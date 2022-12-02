cmake_minimum_required(VERSION 3.20)
project(Client)

set(CMAKE_CXX_STANDARD 14)

#若在命令行中，加-lwsock32
link_libraries(ws2_32 wsock32)

add_executable(Client main.cpp)
