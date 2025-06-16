set(CMAKE_CXX_STANDARD 20)

add_library(net STATIC
    connection.cpp
    connection.h
    protocol.cpp
    protocol.h
)

target_include_directories(net
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_SOURCE_DIR}/../boost/include/boost-1_88
)
