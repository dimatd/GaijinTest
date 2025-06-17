set(CMAKE_CXX_STANDARD 20)

add_library(net STATIC
    connection.h
    protocol.cpp
    protocol.h
	memory.h
)

target_include_directories(net
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_SOURCE_DIR}/boost/include/boost-1_88
)
