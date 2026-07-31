#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Components.h"
#include "engine/Physics.h"
#include "engine/Scene.h"

#include <string>

namespace eng {

void RegisterPhysicsBindings(sol::state& lua) {
    // The `physics` table drives entities that the simulation owns - those
    // carrying both a Collider and a RigidBody. It is the whole interface: a
    // simulated object is never placed, only pushed, and where that puts it is
    // the simulation's answer rather than the script's.
    //
    // Every function here does nothing at all when the entity has no simulated
    // body, or when its body is Static or Kinematic (neither of which is moved
    // by forces). That is deliberate - a script should not have to ask first,
    // and a scene that has not been given rigid bodies yet must keep working
    // exactly as it did.
    //
    // Force or impulse? A FORCE is a continuous push measured in newtons and
    // only means something spread over time, so it must be applied EVERY FRAME
    // for as long as it should act - thrust, lift, drag. An IMPULSE is an
    // instant change of momentum, applied ONCE - a hit, a blast, a knockback.
    // Applying a force once barely moves anything; applying an impulse every
    // frame accelerates without limit.
    sol::table phys = lua.create_named_table("Physics");

    // Physics.applyForce(entity, x, y, z): push in WORLD space.
    phys["applyForce"] = [](Entity& e, float x, float y, float z) {
        ApplyForce(e.id, {x, y, z});
    };
    // Physics.applyLocalForce(entity, x, y, z): push in the entity's OWN
    // frame, so {0,0,-1} is along its nose however it is pointing. This is the
    // natural way to write thrust and aerodynamic forces, which are described
    // relative to the aircraft rather than to the world.
    phys["applyLocalForce"] = [](Entity& e, float x, float y, float z) {
        ApplyLocalForce(e.id, {x, y, z});
    };
    // Physics.applyForceAt(entity, x, y, z, px, py, pz): push in world space
    // at a world POINT. A force applied off-centre also turns the body, which
    // is how a force at a wingtip rolls an aircraft instead of just sliding it.
    phys["applyForceAt"] = [](Entity& e, float x, float y, float z,
                                float px, float py, float pz) {
        ApplyForceAtPoint(e.id, {x, y, z}, {px, py, pz});
    };
    // Turning forces, about the world axes and the entity's own axes. The
    // local one is how roll, pitch and yaw controls are written.
    phys["applyTorque"] = [](Entity& e, float x, float y, float z) {
        ApplyTorque(e.id, {x, y, z});
    };
    phys["applyLocalTorque"] = [](Entity& e, float x, float y, float z) {
        ApplyLocalTorque(e.id, {x, y, z});
    };
    // The instantaneous pair. Apply once, not every frame.
    phys["applyImpulse"] = [](Entity& e, float x, float y, float z) {
        ApplyImpulse(e.id, {x, y, z});
    };
    phys["applyAngularImpulse"] = [](Entity& e, float x, float y, float z) {
        ApplyAngularImpulse(e.id, {x, y, z});
    };

    // Physics.velocity(entity) returns three numbers, which Lua takes as
    // `local vx, vy, vz = Physics.velocity(entity)`. Returning a tuple rather
    // than a table avoids allocating a table every frame for something a
    // flight model reads constantly.
    phys["velocity"] = [](Entity& e) {
        Vector3 v = GetLinearVelocity(e.id);
        return std::make_tuple(v.x, v.y, v.z);
    };
    phys["angularVelocity"] = [](Entity& e) {
        Vector3 v = GetAngularVelocity(e.id);
        return std::make_tuple(v.x, v.y, v.z);
    };
    // Setting velocity overrules the simulation rather than negotiating with
    // it: it throws away whatever forces and collisions had decided. Right for
    // a deliberate discontinuity (launching a projectile at a fixed speed,
    // stopping something dead), wrong for ordinary movement, which should go
    // through forces so that collisions still have a say.
    phys["setVelocity"] = [](Entity& e, float x, float y, float z) {
        // When the body does not exist yet - the normal case for an entity
        // spawned this frame, which joins the simulation at the end of it -
        // this becomes the velocity the body will be born with, so a
        // projectile leaves the muzzle already moving.
        //
        // The entity is already in hand here, so the record is written
        // directly rather than looked up through the active scene. That
        // matters: a spawned entity's onStart is exactly the place this gets
        // called from, and depending on anything more than the entity itself
        // is one more thing that can quietly not be there.
        if (!HasBody(e.id)) {
            if (auto* rb = e.GetComponent<RigidBodyComponent>())
                rb->initialVelocity = {x, y, z};
            return;
        }
        SetLinearVelocity(e.id, {x, y, z});
    };
    phys["setAngularVelocity"] = [](Entity& e, float x, float y, float z) {
        SetAngularVelocity(e.id, {x, y, z});
    };
    // Physics.speed(entity): the length of the velocity, which is what a HUD
    // and a flight model both actually want most of the time.
    phys["speed"] = [](Entity& e) {
        return Vector3Length(GetLinearVelocity(e.id));
    };
    // Physics.hasBody(entity): true when the simulation owns this entity, so
    // a script can offer both a physics path and a hand-moved fallback.
    phys["hasBody"] = [](Entity& e) { return HasBody(e.id); };

    // Physics.setBody(entity, motion [, mass [, gravity [, continuous]]]):
    // hand a spawned entity to the simulation. `motion` is "static",
    // "kinematic" or "dynamic"; anything else is treated as dynamic.
    //
    // Like Scene.setCollider, this only ADDS a rigid body when the entity has
    // none. An entity built in the editor already carries the settings someone
    // chose, and a script default must never overwrite a decision a person
    // made - so calling this on an authored entity leaves it untouched.
    //
    // It exists for entities created at runtime: a bullet is spawned as a bare
    // entity plus a script, and this is how that script says "and I am a solid
    // object that falls".
    phys["setBody"] = [](Entity& e, const std::string& motion,
                          sol::optional<float> mass,
                          sol::optional<float> gravity,
                          sol::optional<bool> continuous) {
        if (e.GetComponent<RigidBodyComponent>()) return;
        RigidBodyComponent& rb = e.AddComponent<RigidBodyComponent>();
        if (motion == "static")         rb.motion = MotionType::Static;
        else if (motion == "kinematic") rb.motion = MotionType::Kinematic;
        else                            rb.motion = MotionType::Dynamic;
        rb.mass          = mass.value_or(rb.mass);
        rb.gravityFactor = gravity.value_or(rb.gravityFactor);
        rb.continuous    = continuous.value_or(rb.continuous);
    };
}

void DescribePhysicsBindings(LuaApiRegistry& api) {
    auto p = api.Table("Physics");
    p.Fn("setBody(entity, motion [, mass [, ...]])",
         "Hand an entity to the simulation: \"static\", \"kinematic\" or \"dynamic\". "
         "Only the arguments you pass are changed");
    p.Fn("applyForce(entity, x, y, z)",
         "Push in WORLD axes. A force acts over time, so apply it EVERY frame it should act");
    p.Fn("applyLocalForce(entity, x, y, z)",
         "Push along the entity's OWN axes - thrust follows the nose as it turns");
    p.Fn("applyForceAt(entity, x, y, z, px, py, pz)",
         "Push at a point, which also produces a turning moment");
    p.Fn("applyImpulse(entity, x, y, z)",
         "An INSTANT change of momentum, applied once - a hit, a blast");
    p.Fn("setVelocity(entity, x, y, z)", "Set velocity outright, ignoring momentum");
    p.Fn("velocity(entity) -> x, y, z",   "Its current velocity, as three numbers");
    p.Fn("speed(entity) -> number",       "How fast it is moving, regardless of direction");
    // Rotation has the same force/impulse split as movement, and for the same
    // reason: a torque is a continuous twist, an angular impulse is one kick.
    p.Fn("applyTorque(entity, x, y, z)",
         "Twist it about WORLD axes. Continuous, so apply every frame it should act");
    p.Fn("applyLocalTorque(entity, x, y, z)",
         "Twist about its OWN axes - roll, pitch and yaw follow the body as it turns");
    p.Fn("applyAngularImpulse(entity, x, y, z)",
         "An instant change of spin, applied once");
    p.Fn("setAngularVelocity(entity, x, y, z)", "Set the spin outright");
    p.Fn("angularVelocity(entity) -> x, y, z",   "Its current spin, as three numbers");
    p.Fn("hasBody(entity) -> bool",
         "Whether the simulation owns this entity. False means every call above does nothing");
}

} // namespace eng
