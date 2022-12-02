cmake_minimum_required(VERSION 3.20)
project(Server)

set(CMAKE_CXX_STANDARD 14)

link_libraries(ws2_32)

add_executable(Server main.cpp)
