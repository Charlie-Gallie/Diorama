add_executable(client source/main.cpp)

target_link_libraries(client PRIVATE solar::engine)

# Dev tools are linked per executable rather than compiled into the engine
# There might be a "studio" executable in the future which will want devtools
target_link_libraries(client PRIVATE $<$<NOT:$<CONFIG:Shipping>>:solar::devtools>)

# Defined as 1/0 rather than defined/undefined so a misspelling in an #if is an actual warning
target_compile_definitions(client PRIVATE SOLAR_DEBUG_TOOLS_ENABLED=$<IF:$<CONFIG:Shipping>,0,1>)

add_dependencies(client game_assets)
