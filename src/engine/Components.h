#pragma once

// The components, gathered.
// ----------------------------------------------------------------------------
// Each component now lives in its own header under components/, so a file that
// needs one type includes one small header instead of every component in the
// engine. This umbrella exists for two reasons:
//
//   * the component FACTORY has to know every type, so something must include
//     them all, and this is that place;
//   * a caller that genuinely wants "the components" - the Inspector, the
//     scene loader - says so in one line rather than eleven.
//
// Including this is never wrong, only broader than it needs to be. New code is
// better off including the one component it uses.

#include "engine/components/Camera.h"
#include "engine/components/Collider.h"
#include "engine/components/Graph.h"
#include "engine/components/Health.h"
#include "engine/components/JSBSim.h"
#include "engine/components/Light.h"
#include "engine/components/Minimap.h"
#include "engine/components/Model.h"
#include "engine/components/RigidBody.h"
#include "engine/components/Script.h"
#include "engine/components/Shape.h"
#include "engine/components/Terrain.h"

#include <memory>
#include <string>

namespace eng {

// Component "factory": given a type name read from a save file (a string like
// "Shape"), build the matching component object. This is how loading turns
// text back into real C++ objects. Returns nullptr for names it doesn't know,
// so an unfamiliar entry in a file is skipped rather than crashing the load.
std::unique_ptr<Component> MakeComponent(const std::string& name);


// A tiny shared store of named numbers that scripts can post to (via the Lua
// `hud.set(name, value)` call) and the editor's HUD can read back. This is how
// a value that lives inside a Lua script (like throttle) reaches the C++ HUD.
// --- HUD drawing support (used by the `draw.*` script API) -------------------

// Named colours for HUD drawing. A script may add to the palette with
// draw.defineColor, so a HUD's colours are data rather than compiled in.
// An unknown name resolves to the default HUD green rather than failing.
void  DefineHudColor(const std::string& name, Color c);
Color HudColor(const std::string& name);   // empty name = the default HUD colour

// Whether HUD drawing is currently legal. The `draw.*` calls only work inside
// the HUD pass; from any other hook they do nothing, because pixel coordinates
// mean nothing in the middle of the 3D pass. Anything that dispatches OnDrawHud
// must bracket it with these.
bool HudDrawAllowed();
void BeginHudPass();
void EndHudPass();

// WHICH CAMERA THE HUD IS BEING DRAWN OVER, and how big the surface is.
//
// Most HUD symbology is placed in the AIRCRAFT'S frame - the pitch ladder and
// the flight path marker are instruments, and an instrument answers "where is
// this relative to my aircraft", which needs no camera at all.
//
// A GUNSIGHT is the exception, and it is a big one. Its whole job is to sit on
// top of the thing you are about to shoot, so it has to be placed where that
// thing actually APPEARS - which depends entirely on the camera. This game is
// drawn from a chase camera set behind and above the aircraft that takes only
// part of its roll, so an aiming mark placed in the aircraft's frame lands
// nowhere near the target on screen. Whoever dispatches OnDrawHud therefore
// says which camera the world was just drawn through.
void SetHudCamera(const Camera3D& cam, int width, int height);

// Project a world point onto the HUD surface. Returns false when the point is
// BEHIND the camera, where a projection produces a mirrored position that looks
// plausible and is completely wrong.
bool WorldToHudScreen(Vector3 world, Vector2& outScreen);

void  SetHudValue(const std::string& key, float value);
float GetHudValue(const std::string& key, float fallback = 0.0f);
// Forget every published HUD value. Called when play starts so each run begins
// fresh (score back to 0, no stale throttle/speed left from the last run).
void  ClearHudValues();

// Global on/off switch for the scripting `input` API. The editor turns it OFF
// while you are typing in a text box or flying the editor's own camera, so the
// game doesn't react to keystrokes meant for the editor. A shipped game leaves
// it on. Defined in the .cpp.
void SetScriptInputEnabled(bool enabled);
} // namespace eng
