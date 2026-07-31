#pragma once

// Every registerXxxBindings function, in one place.
// ----------------------------------------------------------------------------
// Each is implemented in its own file under src/engine/bindings/, beside the
// describeXxxBindings that documents it. ScriptComponent::Load calls them in
// turn to furnish a fresh Lua state.
//
// WHY THEY TAKE A BARE sol::state RATHER THAN THE COMPONENT. None of these
// bindings needs anything from the ScriptComponent that owns the state - they
// reach the world through Scene::Current() and the engine's own free functions.
// Keeping the component out of the signature is what allows them to live in
// separate files at all, and it means a second Lua state (a console, a test,
// a tool) can be given the same API without inventing a component to own it.
//
// Adding a subject:
//   1. src/engine/bindings/XxxBindings.cpp with registerXxx + describeXxx
//   2. declare both here and in LuaApiRegistry.h
//   3. call them from ScriptComponent::Load and GetLuaApiEntries
//   4. add the file to CMakeLists.txt
// The compiler catches every step except the last two, and a missing describe
// only costs documentation, never behaviour.

#define SOL_ALL_SAFETIES_ON 1
#include "sol/sol.hpp"

namespace eng {

// Maths and transforms: Vector3, Quaternion, and the Transform methods a script
// uses to move and aim an entity.
void RegisterTransformBindings(sol::state& lua);

// The Entity type itself - name, tag, transform, and reaching its components.
void RegisterEntityBindings(sol::state& lua);

// Keyboard and mouse queries, gated so the editor can take input back.
void RegisterInputBindings(sol::state& lua);

// The shared named-number store that scripts publish into and the HUD reads.
void RegisterHudBindings(sol::state& lua);

// 2D drawing, legal only inside the HUD pass.
void RegisterDrawBindings(sol::state& lua);

// Particle effect bursts.
void RegisterFxBindings(sol::state& lua);

// Sound playback, positioned and otherwise.
void RegisterAudioBindings(sol::state& lua);

// The rigid-body simulation: bodies, forces, velocities.
void RegisterPhysicsBindings(sol::state& lua);

// The scene's directional light.
void RegisterLightBindings(sol::state& lua);

// The world: finding, spawning, destroying and damaging entities.
void RegisterSceneBindings(sol::state& lua);

} // namespace eng
