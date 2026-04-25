cmake_minimum_required(VERSION 3.27)
project(ping4 VERSION 1.0.0 C)

set(CMAKE_C_STANDARD 17)

add_executable(ping4 ping4.c
        ping4.h
        utils.c
        utils.h
        socket.c
        socket.h
        output.c
        output.h
        packet.c
        packet.h
        pinger.c
        pinger.h)

target_compile_definitions(ping4 PRIVATE "PING4_VERSION=\"${PROJECT_VERSION}\"")
