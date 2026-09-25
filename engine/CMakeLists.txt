add_library(solar_engine STATIC source/Engine.cpp)
add_library(solar::engine ALIAS solar_engine)

target_include_directories(solar_engine PUBLIC include)

target_link_libraries(solar_engine PRIVATE SDL3::SDL3) # Keep SDL out of public headers
