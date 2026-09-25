add_executable(client source/main.cpp)

target_link_libraries(client PRIVATE solar::engine)
