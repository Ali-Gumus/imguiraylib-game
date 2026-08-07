# Minimum CMake version this script needs. FetchContent and modern
# target commands are all fine well below 4.x; 3.24 is a safe floor.
cmake_minimum_required(VERSION 3.24)

# Declares the project. Also tells CMake which language compilers to find.
project(imguiraylib-game LANGUAGES C CXX)

# We want modern C++ (sol2 needs C++17 minimum; 20 gives us nicer features).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies -----------------------------------------------------
# FetchContent downloads source code at configure time and builds it as
# part of OUR build. No installing libraries by hand.
include(FetchContent)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.0          # pin a version so builds are reproducible
)
FetchContent_MakeAvailable(raylib)   # actually download + add its targets

# Turn JPEG loading ON. raylib ships with it OFF (config.h defines
# SUPPORT_FILEFORMAT_JPG as 0) to keep the library small, and the consequence is
# nastier than a missing feature: LoadTexture on a .jpg simply returns an empty
# texture, so a model textured with one draws in flat white with no error
# anywhere. It looks like a lighting bug, or like the texture path being wrong.
#
# Downloaded models very often carry JPEG textures - they are photographs of
# surfaces, which is exactly what JPEG is for - so this is not an edge case.
#
# It works as a plain define because config.h guards every flag with #ifndef,
# so one supplied on the command line wins over the default beneath it. PRIVATE
# because it changes how raylib itself is compiled and nothing else needs it.
target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_JPG=1)

# ---- Dear ImGui ------------------------------------------------------
# ImGui has NO CMakeLists.txt. FetchContent still downloads it, and then
# we define the library target ourselves from its sources.
# We use the "docking" branch: it adds dockable/detachable windows,
# which a Unity-style editor needs.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.8-docking  # must stay compatible with the rlImGui commit below
)
FetchContent_MakeAvailable(imgui)
# After this, ${imgui_SOURCE_DIR} points at the downloaded folder.

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp   # ShowDemoWindow(); drop later if unused
)
# PUBLIC: anyone who links imgui also gets this include path,
# so `#include "imgui.h"` works in OUR code too.
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})

# ---- rlImGui (glue between raylib and ImGui) -------------------------
# Also no CMakeLists. It has no version tags either, so we pin a commit.
FetchContent_Declare(
    rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        ef129d1858373b6fe332c45f85a1dfc1421bebae  # pinned commit (needs ImGui 1.92+)
)
FetchContent_MakeAvailable(rlimgui)

# ---- Lua (the scripting language runtime) ----------------------------
# Fetched as a plain tarball from lua.org (the official source — the
# GitHub repo is just a mirror). No CMakeLists, and it's pure C.
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    # NOTE: 5.5.0 exists, but sol2 v3.5.0 predates it — we pair versions
    # that are tested together (the rlImGui/ImGui lesson).
)
FetchContent_MakeAvailable(lua)

# GLOB: collect every .c file instead of listing ~30 by hand. Caveat you
# should know: CMake won't notice NEW files appearing until reconfigure —
# fine for a frozen third-party lib, avoid for your own code.
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
# lua.c/luac.c are the standalone interpreter/compiler main()s — we embed
# the library, so they'd clash with our own main().
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c
                             ${lua_SOURCE_DIR}/src/luac.c)

add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR}/src)

# ---- imgui-node-editor (visual scripting canvas) ---------------------
# Same story as ImGui itself: sources only, we define the target. Releases
# are years old; master is maintained, so we pin its current commit.
FetchContent_Declare(
    imgui_node_editor
    GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
    GIT_TAG        021aa0ea4da13fed864bafb2a92d4c5205076866
    # Runs once after download, inside the source dir: fixes a duplicate
    # operator* against ImGui >= 1.92 (see the script for details).
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_imgui_node_editor.cmake
)
FetchContent_MakeAvailable(imgui_node_editor)

add_library(imgui_node_editor STATIC
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor_api.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_canvas.cpp
    ${imgui_node_editor_SOURCE_DIR}/crude_json.cpp
)
target_include_directories(imgui_node_editor PUBLIC ${imgui_node_editor_SOURCE_DIR})
target_link_libraries(imgui_node_editor PUBLIC imgui)

# ---- nlohmann/json (scene files) -------------------------------------
# Header-only, ships CMake support: gives the nlohmann_json::nlohmann_json target.
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
)
FetchContent_MakeAvailable(json)

# ---- sol2 (C++ <-> Lua binding layer) --------------------------------
# Header-only and ships real CMake support: MakeAvailable gives us a
# ready "sol2" INTERFACE target (no compiling — just include paths).
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(sol2)

# ---- Jolt Physics (rigid-body simulation) ----------------------------
# The physics engine: it owns the world of solid bodies, moves them under
# forces and gravity, and works out when they touch. We drive it with
# forces rather than setting positions, which is what makes collisions,
# bouncing and momentum come out for free instead of being faked.
#
# Two things about Jolt's build are unusual and both matter:
#
# 1. Its CMakeLists.txt is NOT at the repository root, it sits in a
#    "Build" subfolder. SOURCE_SUBDIR points FetchContent at it.
# 2. Its build script is written to be the TOP-LEVEL project, so by
#    default it reaches out and rewrites global compiler settings. The
#    two options below switch that off, because here Jolt is a guest in
#    someone else's build:
#      - USE_STATIC_MSVC_RUNTIME_LIBRARY: Jolt defaults to the static C
#        runtime (/MT). Everything else here uses the dynamic one (/MD),
#        and mixing the two is a link error ("mismatch detected for
#        RuntimeLibrary"), so force it to agree with us.
#      - OVERRIDE_CXX_FLAGS: stops Jolt replacing CMAKE_CXX_FLAGS_* for
#        the WHOLE project (it disables buffer-security checks and edits
#        optimisation flags — fine for its own code, not ours to change).
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS              OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.6.0        # pinned like every other dependency
    SOURCE_SUBDIR  Build         # where its CMakeLists.txt actually lives
)
FetchContent_MakeAvailable(JoltPhysics)

# ---- JSBSim (flight dynamics) ----------------------------------------
# The aerodynamics of a real aircraft: lift, drag, thrust, control surfaces
# and the moments they produce, read from an aircraft description rather
# than hand-tuned. It replaces the energy model in flight_sim.lua.
#
# Like Jolt, its build script expects to be the top-level project, so the
# options below switch off everything that is not the library itself. Each
# is here because it either fails to build as a guest or drags in a
# dependency this project has no use for:
#   - the Python module needs Python and Cython
#   - the utilities and tests are stand-alone programs
#   - the docs need Doxygen
#   - SYSTEM_EXPAT would look for a system XML parser; JSBSim ships its own
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON_MODULE    OFF CACHE BOOL "" FORCE)
set(BUILD_UTILITIES        OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS             OFF CACHE BOOL "" FORCE)
set(SYSTEM_EXPAT           OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS           OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    jsbsim
    GIT_REPOSITORY https://github.com/JSBSim-Team/jsbsim.git
    GIT_TAG        v1.3.1        # pinned like every other dependency
)
FetchContent_MakeAvailable(jsbsim)

add_library(rlimgui STATIC ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(rlimgui PUBLIC ${rlimgui_SOURCE_DIR})
# rlImGui's code includes both raylib.h and imgui.h, so it must link both.
# PUBLIC again: linking rlimgui hands you imgui + raylib headers too.
target_link_libraries(rlimgui PUBLIC imgui raylib)

# --- Our targets ------------------------------------------------------
# Two targets, one file. engine = static library (window, loop, later
# scene/scripting), editor = the Unity-style executable that links it.

add_library(engine STATIC
    src/engine/Application.cpp
    src/engine/Scene.cpp
    src/engine/Components.cpp
    # One file per component. Components.h remains as an umbrella that includes
    # them all, for the factory and for callers that genuinely want every type.
    src/engine/components/Script.cpp
    src/engine/components/Graph.cpp
    src/engine/components/Shape.cpp
    src/engine/components/Camera.cpp
    src/engine/components/Health.cpp
    src/engine/components/Collider.cpp
    src/engine/components/RigidBody.cpp
    src/engine/components/Light.cpp
    src/engine/components/Model.cpp
    src/engine/components/Terrain.cpp
    src/engine/components/Minimap.cpp
    src/engine/components/JSBSim.cpp
    # The scripting API. One file per subject, each carrying both the sol2
    # bindings and the description of what they expose, so the two cannot drift
    # apart. LuaBindings.h declares them; LuaApiRegistry collects the docs.
    src/engine/LuaApiRegistry.cpp
    src/engine/bindings/TransformBindings.cpp
    src/engine/bindings/EntityBindings.cpp
    src/engine/bindings/InputBindings.cpp
    src/engine/bindings/HudBindings.cpp
    src/engine/bindings/DrawBindings.cpp
    src/engine/bindings/FxBindings.cpp
    src/engine/bindings/AudioBindings.cpp
    src/engine/bindings/PhysicsBindings.cpp
    src/engine/bindings/LightBindings.cpp
    src/engine/bindings/SceneBindings.cpp
    src/engine/bindings/HealthBindings.cpp
    src/engine/bindings/RigidBodyBindings.cpp
    src/engine/bindings/ColliderBindings.cpp
    src/engine/bindings/ModelBindings.cpp
    src/engine/bindings/ShapeBindings.cpp
    src/engine/bindings/TerrainBindings.cpp
    src/engine/bindings/MinimapBindings.cpp
    src/engine/bindings/ScriptBindings.cpp
    src/engine/bindings/JSBSimBindings.cpp
    src/engine/bindings/CameraBindings.cpp
    src/engine/FileDialog.cpp
    src/engine/Lighting.cpp
    src/engine/Particles.cpp
    src/engine/Audio.cpp
    src/engine/Physics.cpp
    # The aerodynamic flight model. This is the ONLY file that includes JSBSim,
    # the same quarantine Physics.cpp keeps around Jolt - see FlightModel.h.
    src/engine/FlightModel.cpp
    src/engine/ModelDefs.cpp
)
# PUBLIC: anything linking engine can #include "engine/Application.h".
target_include_directories(engine PUBLIC src)
# PUBLIC so editor code can also call raylib/ImGui directly for its panels.
target_link_libraries(engine PUBLIC rlimgui sol2 lua nlohmann_json::nlohmann_json Jolt libJSBSim)
# JSBSim's headers are not exported by its target, so the path is given here.
# PRIVATE, and that is the intention rather than an oversight: JSBSim is to be
# quarantined inside one file the way Jolt is inside Physics.cpp (see §5.19 of
# the handover). Anything that can include <FGFDMExec.h> can leak its types into
# a header, and JSBSim brings its own SGPath, FGColumnVector3 and a namespace
# full of short names into a project that already has raylib's globals.
target_include_directories(engine PRIVATE ${jsbsim_SOURCE_DIR}/src)
# Win32 native file dialogs (FileDialog.cpp): common dialog + window APIs.
target_link_libraries(engine PUBLIC comdlg32 user32)
# sol2's "safety on" mode: friendly errors instead of crashes when Lua
# and C++ disagree about types. Costs a little speed; worth it always.
target_compile_definitions(engine PUBLIC SOL_ALL_SAFETIES_ON=1)

add_executable(editor
    src/editor/main.cpp
    src/editor/ScriptGraph.cpp
)
# Node editor is EDITOR-only: the shipped game runs generated .lua and
# never shows a node canvas, so the engine must not depend on it.
target_link_libraries(editor PRIVATE engine imgui_node_editor)

# Bake the project root into the EDITOR (dev tool -> works directly on
# the source assets/, like Unity does). The shipped game will instead use
# assets copied next to its exe — that's what the post-build copy is for.
target_compile_definitions(editor PRIVATE PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}")

# After every build, mirror assets/ next to the exe so relative paths
# like "assets/scripts/spin.lua" work when VS launches it. copy_directory
# only copies changed files, so this is cheap.
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:editor>/assets)

# =============================================================================
# THE GAME -- the standalone runtime a player runs.
#
# A second executable over the SAME engine library. It shares every subsystem
# with the editor and duplicates none of them; what it leaves out is the editor
# itself. Note what is deliberately absent below:
#
#   * imgui_node_editor       - a player never opens a node canvas
#   * PROJECT_ROOT_DIR        - the game must NOT reach back into the source
#                               tree. It uses the assets copied beside its exe,
#                               which is what makes the build folder movable to
#                               a machine that has never seen this project.
# =============================================================================
add_executable(game src/game/main.cpp)
target_link_libraries(game PRIVATE engine)

# NO CONSOLE WINDOW BEHIND THE GAME. A console-subsystem exe opens a black
# terminal alongside itself, which is right for a developer tool and wrong for a
# shipped product - the editor keeps its console on purpose, and this is exactly
# the difference between the two.
#
# The obvious way to do this, CMake's WIN32_EXECUTABLE, is NOT used, because it
# also demands a `WinMain` entry point: the Windows subsystem has its own
# signature, and a plain `main` no longer satisfies the linker (the error is an
# unresolved WinMain, which reads as a missing library rather than as a
# subsystem setting). Writing a WinMain would mean a second entry point that
# only exists on one platform.
#
# Instead the subsystem is set to WINDOWS while the entry point is pointed back
# at the ordinary C runtime startup, which is what calls `main`. The result is
# one portable entry point and no console.
if(MSVC)
    target_link_options(game PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# The same asset mirror the editor gets, and for a stronger reason: this exe has
# no other way to find its files.
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:game>/assets)

# -----------------------------------------------------------------------------
# `package_game` -- collect a distributable build into dist/<config>/.
#
# Building the `game` target already produces a working exe with its assets
# beside it, but it sits in a build tree full of object files, the editor, and
# every dependency's intermediate output. This copies just the two things a
# player needs into a clean folder that can be zipped and sent.
#
# It is NOT part of the default build (EXCLUDE_FROM_ALL via being a custom
# target nothing depends on), because copying the whole asset tree on every
# compile is a cost only worth paying when actually shipping something. Build it
# by name:  cmake --build build --config Release --target package_game
#
# `package_game` DEPENDS on `game`, so asking for a package always builds first and a
# stale exe cannot be shipped by accident.
# -----------------------------------------------------------------------------
add_custom_target(package_game
    DEPENDS game
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_SOURCE_DIR}/dist/$<CONFIG>
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist/$<CONFIG>
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:game> ${CMAKE_SOURCE_DIR}/dist/$<CONFIG>/
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets ${CMAKE_SOURCE_DIR}/dist/$<CONFIG>/assets
    COMMENT "Packaging the game into dist/$<CONFIG>/"
    VERBATIM)
