cmake_minimum_required(VERSION 3.1)

add_executable(webproject
	webproject.cpp
	builder.cpp
	server.cpp
)

target_link_libraries(webproject
	${STANDARD_LIBRARIES}
)
add_dependencies(webproject webproject_version)

