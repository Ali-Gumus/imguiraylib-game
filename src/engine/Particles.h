#pragma once

// ============================================================================
// Particles: short-lived visual effects - explosions, sparks, muzzle flashes.
// ----------------------------------------------------------------------------
// A "particle" here is deliberately tiny: a point with a position, a velocity,
// a lifetime and a colour. Hundreds of them thrown outward from one spot read
// as an explosion; a handful read as sparks. There is no physics involved and
// none is wanted - particles never collide with anything, which is exactly why
// a scene can afford thousands of them.
//
// Effects are fired as one-shot BURSTS at a point in the world:
//
//     eng::Burst(eng::FxPreset::Explosion, position);
//
// and from Lua as `fx.burst("explosion", x, y, z)`.
//
// The particles live in a fixed-size pool that is allocated once. A firefight
// is the worst possible moment to be asking the operating system for memory,
// so the system never grows: when the pool is full the oldest particle is
// recycled, which is invisible in practice and keeps every frame predictable.
// ============================================================================

#include "raylib.h"     // Vector3, Color, Camera3D, Texture2D
#include "engine/Scene.h"  // EntityID, and the Scene attached bursts look up in

#include <string>
#include <vector>

namespace eng {

// Create the resources the system needs. Call ONCE after the window exists,
// because the particle texture lives on the GPU. Returns false if that texture
// could not be created, in which case nothing is drawn and nothing crashes.
bool InitParticles();

// Free those resources. Call once at shutdown, before the window closes.
void ShutdownParticles();

// Throw a burst of particles outward from `pos`, using the effect of that name
// (the names are whatever assets/scripts/effects.lua defines). `scale`
// multiplies the size and speed of the whole effect, so one recipe can serve a
// rifle round and a bomb.
//
// `inherit` is the velocity of whatever emitted the burst, carried by every
// particle for its whole life ON TOP OF its own outward spread. Without it, an
// effect fired from something moving fast is left behind the instant it is born:
// a muzzle flash on a jet travelling 200 units a second falls 40 units back over
// a fifth of a second. Real exhaust gas leaves a moving gun already carrying the
// gun's motion, and that is exactly what this reproduces.
//
// It is deliberately immune to the effect's own drag and gravity. Those describe
// how the burst spreads and settles, and applying them to the emitter's motion
// as well drags the entire effect backwards through whatever fired it - the
// faster the emitter, the further back. See Particle::carrier in Particles.cpp.
//
// An unrecognised name draws a small neutral puff instead of failing: a typo in
// a gameplay script should not interrupt play.
void BurstNamed(const char* preset, Vector3 pos, float scale = 1.0f,
                Vector3 inherit = {0.0f, 0.0f, 0.0f});

// The same, but PINNED to an entity: every frame the burst is replaced at that
// entity's current position and orientation, offset by `anchor` measured in the
// entity's OWN axes. Only the particles' own spread moves them from there.
//
// USE THIS FOR ANYTHING THAT IS PART OF THE OBJECT rather than released by it -
// an afterburner flame above all. Handing a burst the emitter's velocity keeps
// it in step only while the emitter travels in a STRAIGHT LINE; an aircraft
// under any g is curving, and the effect is left behind toward its belly at a
// rate that grows with the turn. Measured on a 0.2 s effect: 0.19 m at 2.2 g,
// 0.45 m at 3.9 g, and the opposite way when pushing over.
//
// Smoke, sparks and debris genuinely are released into the air and should keep
// using BurstNamed with the emitter's velocity: drifting is what they do.
void BurstNamedOn(const char* preset, EntityID entity, Vector3 anchor,
                  float scale = 1.0f);

// Advance every live particle by `dt` seconds and retire the expired ones.
// Call once per frame, before drawing.
// `scene` is needed only to find the entities that attached particles are
// pinned to. An attached particle whose entity has gone is cut loose where it
// was and finishes its life there, so a burst never vanishes mid-flight because
// the thing that made it was destroyed.
void UpdateParticles(Scene& scene, float dt);

// Draw every live particle as a camera-facing square. Call inside an active
// BeginMode3D/EndMode3D block, after the scene, passing the same camera the
// scene was drawn with.
void DrawParticles(const Camera3D& camera);

// Remove every particle immediately. Effects are runtime state, so play mode
// starting or stopping wipes them, exactly as the HUD value store is wiped.
void ClearParticles();

// How many particles are currently alive, for the editor's statistics readout.
int AliveParticleCount();

// The world position of one live particle, by index. False if there is no
// particle there. For tests and diagnostics - nothing in the game needs it, but
// "is the effect where it should be?" is otherwise a question only eyes can ask.
bool ParticlePosition(int index, Vector3& out);

// Re-read assets/scripts/effects.lua, which is where every effect recipe is
// written. Called once at startup and again whenever play begins, so tuning an
// effect only means editing that file and pressing Play - no rebuild.
//
// The file runs in the particle system's own small Lua interpreter, with one
// function available:
//
//     fx.define("explosion", { count = 60, life_min = 0.5, ... })
//
// Any field left out keeps the value the preset already had, so a file can
// change one number without restating the rest. A name that does not exist yet
// creates a new preset, which scripts can then fire by that name.
// Returns false if the file is missing or has an error; the presets built into
// the engine stay in force in that case, so effects never simply stop working.
bool ReloadEffectPresets();

// The error from the last ReloadEffectPresets, or "" if it succeeded. The
// editor shows this so a typo in the file is visible rather than silent.
const char* EffectPresetError();

// The names of every effect currently defined, in alphabetical order. The node
// editor lists these in a dropdown, so an effect invented in effects.lua can be
// picked in a graph without anyone typing its name or touching C++.
const std::vector<std::string>& EffectPresetNames();

} // namespace eng
