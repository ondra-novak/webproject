include_directories(BEFORE ${CMAKE_BINARY_DIR}/src)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/src/webproject")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/version" "webproject/version")
