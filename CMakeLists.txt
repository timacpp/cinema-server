cmake_minimum_required(VERSION 3.21)
project(cinema_server)

set(CMAKE_CXX_STANDARD 17)

add_executable(ticket_server src/ticket_server.cpp src/flags.h src/ensure.h src/buffer.h)
