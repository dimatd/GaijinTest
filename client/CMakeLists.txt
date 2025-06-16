add_executable(client
    client.cpp
)

target_link_libraries(client PRIVATE net)

target_include_directories(net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/../boost/include/boost-1_88
)
