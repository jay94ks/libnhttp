add_executable(nhttpd nhttpd/main.cpp)

target_link_libraries(nhttpd PRIVATE nhttp::nhttp nhttp_warnings)
