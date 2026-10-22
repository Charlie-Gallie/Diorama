cmake_minimum_required(VERSION 3.28)

project(SolarEngine VERSION 0.1.0 LANGUAGES C CXX) # Using C for SDL3

set(CMAKE_CXX_STANDARD 20) # Set C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Fail if C++20 isn't available
set(CMAKE_CXX_EXTENSIONS OFF) # Disable MSVC extensions so that it still compiles on Linux

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CONFIGURATION_TYPES "Debug;Development;Shipping" CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS_DEVELOPMENT "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_DEVELOPMENT "${CMAKE_CXX_FLAGS_DEVELOPMENT}")
set(CMAKE_CXX_FLAGS_SHIPPING "${CMAKE_CXX_FLAGS_RELEASE}")

set(CMAKE_C_FLAGS_DEVELOPMENT "${CMAKE_C_FLAGS_RELEASE}") # SDL3 is C
string(REPLACE "/DNDEBUG" "" CMAKE_C_FLAGS_DEVELOPMENT "${CMAKE_C_FLAGS_DEVELOPMENT}")
set(CMAKE_C_FLAGS_SHIPPING "${CMAKE_C_FLAGS_RELEASE}")

set(CMAKE_EXE_LINKER_FLAGS_DEVELOPMENT "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}")
set(CMAKE_EXE_LINKER_FLAGS_SHIPPING "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /debug")

set(CMAKE_SHARED_LINKER_FLAGS_DEVELOPMENT "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO}") # SDL3.dll
set(CMAKE_SHARED_LINKER_FLAGS_SHIPPING "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /debug")

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase")

# Executables and DLLs share one directory per config so SDL3.dll sits next to client.exe
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(FetchContent)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE
    SYSTEM EXCLUDE_FROM_ALL)

# Dear ImGui ships no CMakeLists.txt, so MakeAvailable only
# downloads it and the target below is made ourself
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b
    GIT_SHALLOW    TRUE
    SYSTEM EXCLUDE_FROM_ALL)

# spdlog is built as a compiled library rather than header-only, so fmt is not
# pulled into every translation unit that logs.
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE
    SYSTEM EXCLUDE_FROM_ALL)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3
    GIT_SHALLOW    TRUE
    SYSTEM EXCLUDE_FROM_ALL)

# Use std::format (C++20) instead of the bundled fmt.
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3 imgui spdlog glm)

add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp"
)

# SYSTEM so ImGui's own warnings stay out of the build output
target_include_directories(imgui SYSTEM PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)

# The ImGui backend headers need SDL
# This stops at solar_engine (which links imgui privately) so SDL still does not reach engine consumers
target_link_libraries(imgui PUBLIC SDL3::SDL3)

set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")
set_target_properties(spdlog PROPERTIES FOLDER "ThirdParty")

# --- Feature flags ----
# Carried on an interface target and linked PUBLIC through the engine, so every target agrees.
# Defined as 1/0 rather than defined/undefined
# a misspelling in an #if is then a warning rather just being off
add_library(solar_flags INTERFACE)
add_library(solar::flags ALIAS solar_flags)

target_compile_definitions(solar_flags INTERFACE
    SOLAR_SHIPPING=$<IF:$<CONFIG:Shipping>,1,0>

    # __FILE__ must not reach a shipped binary, and trace arguments should not
    # even be evaluated there.
    SOLAR_LOG_LOCATION_ENABLED=$<IF:$<CONFIG:Shipping>,0,1>
    SOLAR_LOG_TRACE_ENABLED=$<IF:$<CONFIG:Shipping>,0,1>
)

# --- Warnings ---
# Linked PRIVATE by our own targets only, so third-party code keeps its own settings
# C4668 and -Wundef are what make an undefined flag in an #if a warning, which /W4 alone leaves off
add_library(solar_warnings INTERFACE)
add_library(solar::warnings ALIAS solar_warnings)

target_compile_options(solar_warnings INTERFACE
    $<$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>:/W4 /w14668>
    $<$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>:-Wall -Wextra -Wpedantic -Wundef>
)

# --- Assets ---
# Copied next to the executable so paths can be resolved from SDL_GetBasePath rather than the working directory
# which differs between F5, a shortcut and a shipped install.
# copy_directory_if_different runs every build but only touches files that actually changed.
add_custom_target(game_assets ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/assets"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/assets"
    COMMENT "Copying assets"
    VERBATIM
)

add_subdirectory(engine)
add_subdirectory(devtools)
add_subdirectory(apps/client)
