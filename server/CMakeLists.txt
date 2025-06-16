add_executable(server
    server.cpp
    config_store.cpp
    config_store.h
    server_dispatcher.cpp
    server_dispatcher.h
)

target_link_libraries(server PRIVATE net)

target_include_directories(net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/boost/include/boost-1_88
	${CMAKE_SOURCE_DIR}/immer/
)
