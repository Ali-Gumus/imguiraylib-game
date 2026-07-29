#pragma once

// ============================================================================
// ModelDefs: named model set-ups, described as data.
// ----------------------------------------------------------------------------
// Getting an imported model to sit correctly on an entity takes four separate
// numbers-worth of fiddling: which file, how big it should be, which way it
// faces, and where its pivot is relative to its body. The helicopter in this
// project needs a scale of 0.01, a 180-degree turn, and a pivot shift of
// (0, -240, 250) before it looks like an aircraft rather than a distant speck
// pointing the wrong way.
//
// That is a lot to repeat, and a spawning script should not have to know any of
// it. So a model set-up is given a NAME here, and everything else refers to the
// name:
//
//     scene.spawn("Enemy", x, y, z, dx, dy, dz, script, "enemy", 3, "heli")
//
// This follows the same pattern as effects.lua and sounds.lua - the machinery
// is C++, the data is Lua, and the file is re-read on every Play, so retuning a
// model means editing a number and pressing Play with no rebuild. It also means
// the node editor can offer a DROPDOWN of whatever is defined, the way the FX
// Burst and Play Sound nodes already do, instead of asking anyone to type a
// file path into a node.
//
// Everything degrades quietly: an unknown name simply leaves the entity with
// its default cube, which is visible without being fatal.
// ============================================================================

#include "raylib.h"

#include <string>
#include <vector>

namespace eng {

struct Entity;

// One named model set-up.
struct ModelDef {
    std::string name;
    std::string file;                     // path, relative to the project root
    float       scale = 1.0f;             // uniform entity scale to apply
    Vector3     rotationOffset{0, 0, 0};  // euler degrees, to face -Z
    Vector3     positionOffset{0, 0, 0};  // shift onto the entity's origin
};

// Re-read assets/scripts/models.lua. Called at startup and on each Play.
bool ReloadModelDefs();

// The error from the last load, or "" if it succeeded.
const char* ModelDefError();

// Every defined name, sorted. The node editor lists these in a dropdown.
const std::vector<std::string>& ModelDefNames();

// Look one up, or nullptr if the name is unknown.
const ModelDef* FindModelDef(const std::string& name);

// Give `e` the model described by `name`: adds a ModelComponent carrying the
// file and both offsets, and sets the entity's scale.
//
// Does nothing if the name is unknown, or if the entity ALREADY has a
// ModelComponent - an entity built in the editor keeps the model someone chose
// for it, the same add-only-if-missing rule scene.set_collider follows.
// Returns true if a model was actually added.
bool ApplyModelDef(Entity& e, const std::string& name);

// Load every defined model's file now, so that nothing spawned later has to
// stop the game to read one off disk. Call when play begins: a pause there is
// expected and barely noticed, whereas the same pause when a wave of enemies
// appears is the most conspicuous moment it could possibly happen.
// Needs the window to exist, since loading a model talks to the graphics device.
void PreloadModelDefs();

} // namespace eng
