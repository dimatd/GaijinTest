cmake_minimum_required(VERSION 3.14)
project(Test)

set(CMAKE_CXX_STANDARD 20)

# Подпроекты
add_subdirectory(net)
add_subdirectory(client)
add_subdirectory(server)


