#pragma once

// ============================================================================
// Physics: the rigid-body simulation, built on the Jolt Physics library.
// ----------------------------------------------------------------------------
// Everything the engine has done until now MOVES THINGS BY HAND: a script sets
// an entity's position each frame, and "collision" means measuring the distance
// between two points. That is cheap and predictable, but nothing falls, nothing
// bounces off anything, and nothing carries momentum.
//
// A physics engine inverts that. You describe each object once - its shape, its
// mass, how bouncy and how slippery it is - and then you only ever apply
// FORCES to it. The simulation works out the motion that results, and works out
// when two shapes overlap and how to push them apart. Gravity, sliding,
// tumbling and impacts all fall out of that one rule instead of being written
// case by case.
//
// The vocabulary, since it is used throughout:
//   * BODY        - one simulated object: a shape plus a mass plus a position.
//   * STATIC body - never moves (the ground). Cheap: the simulation skips it.
//   * DYNAMIC body- moved by forces and collisions. The interesting kind.
//   * KINEMATIC   - moved by YOUR code, but still pushes dynamic bodies out of
//                   the way. The right choice for something under script or
//                   animation control that must still shove things around.
//   * BROADPHASE  - the coarse first pass that asks "which pairs of objects are
//                   anywhere near each other?", so the expensive exact test
//                   only runs on the few pairs that could possibly touch.
//   * LAYER       - a category used to skip whole classes of pair: two pieces
//                   of scenery can never collide with each other, so the
//                   simulation should never even ask.
//
// This header deliberately exposes NO Jolt types. Two reasons, and the second
// one is not theoretical:
//   1. Layering: nothing outside this file should have to know which physics
//      library is underneath, so it can be replaced without touching callers.
//   2. Jolt and raylib both define types with the SAME NAMES at their top
//      level - `Color`, `Ray`, `Plane` among others. raylib puts its types in
//      the global namespace, so a file that has both visible must qualify
//      every Jolt name as `JPH::...` and must never say `using namespace JPH`.
//      Keeping Jolt inside Physics.cpp confines that hazard to one file, the
//      same quarantine trick FileDialog.cpp uses for <windows.h>.
//
// The world is created once at startup and RESET (emptied) on Play and Stop,
// because a simulation is runtime state exactly like the HUD values and the
// particles: nothing it contains should survive the run that produced it.
// ============================================================================

#include "raylib.h"      // Vector3
#include "engine/Scene.h" // Scene, and EntityID - which every call below names

namespace eng {

// --- Lifetime ---------------------------------------------------------------

// Build the physics world. Call ONCE at startup. Unlike the lighting and
// particle systems this needs no window, because physics never touches the GPU
// - but it is kept in the same place for consistency.
//
// Returns false if the world could not be created, in which case every other
// function here does nothing and the engine behaves exactly as it did before
// physics existed. Failure is not fatal by design: a scene with no physics
// bodies should still open and play.
bool InitPhysics();

// Destroy the world and release Jolt's global state. Call once at shutdown.
void ShutdownPhysics();

// Did the world get built? Everything below is safe to call either way.
bool IsPhysicsReady();

// Why initialisation failed, or "" if it did not. Shown in the toolbar so a
// broken physics world is never a silent mystery.
const char* PhysicsError();

// --- Per-run state ----------------------------------------------------------

// Remove every body and forget every leftover of the previous run. Call on
// BOTH Play and Stop: on Play so the run starts from the authored scene rather
// than from wherever the last run finished, and on Stop so a stopped editor is
// not still holding bodies for entities that have been restored underneath it.
void ResetPhysics();

// Advance the simulation by `dt` seconds of game time and keep it in step with
// the scene. Call once per frame while playing (and NOT while stopped - a
// paused world should not drift). It does three things in this order:
//
//   1. RECONCILE. Any entity that has both a Collider and a RigidBody but no
//      simulated body yet gets one built from its current transform; any body
//      whose entity has been destroyed is removed. Doing this every frame
//      rather than once at the start is what makes spawned and destroyed
//      entities work without the spawn code having to know physics exists.
//   2. STEP. Kinematic bodies are first moved to wherever scripts have put
//      their entities, then the simulation runs.
//   3. WRITE BACK. Every dynamic body's new position is copied onto its
//      entity, converted from world space into the entity's local transform.
//
// `dt` is not handed straight to the simulation. See the note on fixed
// timesteps in Physics.cpp: physics is only stable when it is stepped by a
// CONSTANT amount, so this accumulates real time and runs as many equal-sized
// steps as have been earned.
void UpdatePhysics(Scene& scene, float dt);

// --- World settings ---------------------------------------------------------

// The acceleration applied to every dynamic body, in world units per second
// squared. The default is {0, -9.81, 0}: real gravity, on the assumption that
// one world unit is one metre. That assumption is worth holding to, because
// every other physical constant (mass in kilograms, force in newtons) only
// produces believable motion if the length unit agrees with them.
void    SetPhysicsGravity(Vector3 g);
Vector3 GetPhysicsGravity();

// --- Driving a body ---------------------------------------------------------
// This is the entire vocabulary for moving something the simulation owns. You
// never set a simulated object's position: you push it, and the simulation
// works out where that lands it. Everything here is addressed by EntityID and
// does nothing at all if that entity has no simulated body, so a script may
// call it on anything without checking first.
//
// FORCE vs IMPULSE is the distinction to get right, and the units make it
// clear. A FORCE (newtons) is a continuous push that only means something
// spread over time - a rocket motor, a wing's lift, gravity. Apply it every
// frame for as long as it should act. An IMPULSE (newton-seconds) is an
// instant change in momentum - a hit, an explosion, a jump. Apply it once.
// Applying a force once does almost nothing; applying an impulse every frame
// produces an object that accelerates absurdly.
//
// Only DYNAMIC bodies respond. A static or kinematic body is not moved by
// forces by definition, so these are silently ignored for one - which is the
// usual reason a push appears to do nothing.

// Does this entity currently have a simulated body?
bool HasBody(EntityID id);

// Push in WORLD space, through the body's centre of mass, in newtons.
void ApplyForce(EntityID id, Vector3 force);

// Push in the entity's OWN space, so {0,0,-1} is along its nose whichever way
// it is pointing. This is what a thrust or lift model wants: aerodynamic
// forces are naturally described in the aircraft's frame, not the world's.
void ApplyLocalForce(EntityID id, Vector3 force);

// Push in world space at a particular world POINT rather than at the centre of
// mass. Anything applied off-centre also rotates the body, which is how a
// force at a wingtip rolls an aircraft rather than merely sliding it.
void ApplyForceAtPoint(EntityID id, Vector3 force, Vector3 worldPoint);

// Turning force ("moment"), in newton-metres, about the world axes and about
// the entity's own axes respectively. Local torque is the natural way to
// express roll, pitch and yaw control.
void ApplyTorque(EntityID id, Vector3 torque);
void ApplyLocalTorque(EntityID id, Vector3 torque);

// Instantaneous changes in momentum. Use for hits, blasts and knockback.
void ApplyImpulse(EntityID id, Vector3 impulse);
void ApplyAngularImpulse(EntityID id, Vector3 impulse);

// Read and write velocity directly, in world units per second (and radians per
// second for the angular one).
//
// SETTING velocity overrules the simulation rather than negotiating with it,
// which makes it the wrong tool for ordinary movement - it discards whatever
// forces and collisions had decided. It is the right tool for a deliberate
// discontinuity: launching a projectile at a fixed speed, or bringing a body
// to a dead stop. Reading is always safe, and returns zeros for an entity with
// no body.
Vector3 GetLinearVelocity(EntityID id);
void    SetLinearVelocity(EntityID id, Vector3 v);
Vector3 GetAngularVelocity(EntityID id);
void    SetAngularVelocity(EntityID id, Vector3 v);

// --- Diagnostics ------------------------------------------------------------

// How many bodies the world currently holds. Zero until bodies are added.
int PhysicsBodyCount();

// How long the last frame's simulation took, in milliseconds. Physics is the
// usual first suspect when a frame gets slow, so this is reported in the
// toolbar next to the frame time.
float PhysicsStepMs();

} // namespace eng
