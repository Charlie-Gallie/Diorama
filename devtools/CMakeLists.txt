add_library(solar_devtools STATIC
	"source/DevTools.cpp"
	"source/Performance.cpp"
)
add_library(solar::devtools ALIAS solar_devtools)

target_include_directories(solar_devtools PUBLIC include)

# Consumers of devtools inevitably use engine types too.
target_link_libraries(solar_devtools PUBLIC solar::engine)

# ImGui is how the panels are drawn, but that is an implementation detail so devtools
# own headers must not expose it
target_link_libraries(solar_devtools PRIVATE imgui)
