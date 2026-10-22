add_library(solar_engine STATIC
	"source/DebugClient.cpp"
	"source/Engine.cpp"
	"source/platform/Window.cpp"
	"source/core/Error.cpp"
	"source/platform/Events.cpp"
	"source/platform/Platform.cpp"
	"source/platform/Input.cpp"
	"source/core/Log.cpp"
	"source/render/Renderer.cpp"
	"source/core/Filesystem.cpp"
	"source/voxel/VoxImport.cpp"
	"source/render/ImGuiLayer.cpp"
)
add_library(solar::engine ALIAS solar_engine)

target_include_directories(solar_engine PUBLIC include)

# Lets a dev build read assets from the source tree, so saving a model shows up in a running
# game instead of waiting for the next build to copy it across. Shipping has no source tree
target_compile_definitions(solar_engine PRIVATE
	$<$<NOT:$<CONFIG:Shipping>>:SOLAR_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}/\">
)

# PUBLIC: feature flags must be identical in every target that links the engine
target_link_libraries(solar_engine PUBLIC solar::flags)
target_link_libraries(solar_engine PRIVATE solar::warnings)

# PUBLIC: glm types appear in public headers (solar/core/Math.hpp)
target_link_libraries(solar_engine PUBLIC glm::glm)

target_link_libraries(solar_engine PRIVATE SDL3::SDL3) # Keep SDL out of public headers
target_link_libraries(solar_engine PRIVATE imgui) # Also keep out of public headers
target_link_libraries(solar_engine PRIVATE spdlog::spdlog) # Backend for solar::Log*, never exposed

# --- Shaders ---
# HLSL is compiled to SPIR-V at build time by dxc from the Vulkan SDK
# The game ships only the bytecode, so no compiler is needed at runtime.

find_program(DXC_EXECUTABLE dxc
	HINTS "$ENV{VULKAN_SDK}/Bin"
	DOC "DirectX Shader Compiler, used to produce SPIR-V"
	REQUIRED
)

# Compiled shaders sit next to the executable so the runtime can find them with
# a path relative to the binary rather than the working directory.
set(SOLAR_SHADER_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders")

# in_source: path relative to this directory. in_profile: dxc target profile.
function(solar_compile_shader in_source in_profile out_compiled)
	cmake_path(GET in_source STEM LAST_ONLY stem)  # "Fullscreen.vert.hlsl" -> "Fullscreen.vert"
	set(compiled "${SOLAR_SHADER_OUTPUT_DIR}/${stem}.spv")

	add_custom_command(
		OUTPUT "${compiled}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${SOLAR_SHADER_OUTPUT_DIR}"
		COMMAND "${DXC_EXECUTABLE}"
			-T ${in_profile}
			-E main
			-spirv
			-fspv-target-env=vulkan1.0
			$<$<NOT:$<CONFIG:Shipping>>:-Zi>
			-Fo "${compiled}"
			"${CMAKE_CURRENT_SOURCE_DIR}/${in_source}"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${in_source}"
		COMMENT "Compiling shader ${in_source}"
		VERBATIM
		COMMAND_EXPAND_LISTS
	)

	set(${out_compiled} "${compiled}" PARENT_SCOPE)
endfunction()

solar_compile_shader("shaders/Fullscreen.vert.hlsl" vs_6_0 fullscreen_vert)
solar_compile_shader("shaders/Fullscreen.frag.hlsl" ps_6_0 fullscreen_frag)
solar_compile_shader("shaders/Present.frag.hlsl" ps_6_0 present_frag)

# A custom target is what actually gets built; the custom commands above only run
# when something depends on their output.
add_custom_target(solar_shaders DEPENDS
	"${fullscreen_vert}"
	"${fullscreen_frag}"
	"${present_frag}"
)

# Anything linking the engine builds the shaders first.
add_dependencies(solar_engine solar_shaders)

# Show the HLSL in the IDE without compiling it as C++.
target_sources(solar_engine PRIVATE
	"shaders/Fullscreen.vert.hlsl"
	"shaders/Fullscreen.frag.hlsl"
	"shaders/Present.frag.hlsl"
)
set_source_files_properties(
	"shaders/Fullscreen.vert.hlsl"
	"shaders/Fullscreen.frag.hlsl"
	"shaders/Present.frag.hlsl"
	PROPERTIES HEADER_FILE_ONLY TRUE
)
