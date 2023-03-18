add_custom_target(webproject_version
    ${CMAKE_COMMAND} -D SRC=${CMAKE_CURRENT_LIST_DIR}/version.h.in
          -D DST=${CMAKE_BINARY_DIR}/src/webproject_version.h
          -D ROOT=${CMAKE_CURRENT_LIST_DIR}/..
          -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
          -P ${CMAKE_CURRENT_LIST_DIR}/GenerateVersionHeader.cmake
          )
