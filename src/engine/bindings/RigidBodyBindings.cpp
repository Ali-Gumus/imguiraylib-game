#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/RigidBody.h"

namespace eng {

void RegisterRigidBodyBindings(sol::state& lua) {
    // The three motion types, named rather than numbered. Which one a body has
    // decides whether the simulation may move it at all, so a script reading
    // `body.motion == MotionType.Dynamic` is asking a real question.
    lua.new_enum<MotionType>("MotionType", {
        {"Static",    MotionType::Static},
        {"Kinematic", MotionType::Kinematic},
        {"Dynamic",   MotionType::Dynamic},
    });

    lua.new_usertype<RigidBodyComponent>("RigidBody",
        "motion",          &RigidBodyComponent::motion,
        "mass",            &RigidBodyComponent::mass,
        "friction",        &RigidBodyComponent::friction,
        "restitution",     &RigidBodyComponent::restitution,
        "linearDamping",   &RigidBodyComponent::linearDamping,
        "angularDamping",  &RigidBodyComponent::angularDamping,
        "gravityFactor",   &RigidBodyComponent::gravityFactor,
        "continuous",      &RigidBodyComponent::continuous,
        "initialVelocity", &RigidBodyComponent::initialVelocity
    );
    RegisterComponentAccess<RigidBodyComponent>(lua, "RigidBody");
}

void DescribeRigidBodyBindings(LuaApiRegistry& api) {
    api.Usertype("Entity", "entity")
        .Method("addComponent_RigidBody() -> RigidBody", "Add a RigidBody and return it")
        .Method("getComponent_RigidBody() -> RigidBody", "Its RigidBody, or nil");

    auto b = api.Usertype("RigidBody", "body");
    b.Prop("motion",          "MotionType.Static, .Kinematic or .Dynamic");
    b.Prop("mass",            "Kilograms. Only meaningful for a Dynamic body");
    b.Prop("friction",        "How much it resists sliding, 0 to 1");
    b.Prop("restitution",     "How much it bounces, 0 to 1");
    b.Prop("linearDamping",   "Slows movement over time, standing in for air resistance");
    b.Prop("angularDamping",  "Slows spin over time");
    b.Prop("gravityFactor",   "Multiplier on gravity. 0 makes it float");
    b.Prop("continuous",      "Sweep the whole path each step. Needed for anything fast enough to jump past a wall");
    b.Prop("initialVelocity", "The velocity the body is BORN with, for something spawned already moving");

    api.Table("MotionType")
        .Value("Static",    "Never moves. Terrain and scenery")
        .Value("Kinematic", "Moved by a script, and shoves dynamic bodies aside")
        .Value("Dynamic",   "Moved by the simulation. The only kind forces act on");
}

} // namespace eng
