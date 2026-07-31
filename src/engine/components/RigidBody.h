#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
// ============================================================================
// RigidBodyComponent: hands an entity over to the physics simulation.
// ----------------------------------------------------------------------------
// On its own an entity is moved by whatever code sets its transform. Add a
// RigidBody and that reverses: the simulation now owns where the entity is,
// and the only way to move it is to apply forces. Gravity, sliding, tumbling
// and bouncing off things then happen by themselves.
//
// It is OPT-IN and needs a ColliderComponent beside it, because the simulation
// has to know what shape the object occupies. An entity with a Collider but no
// RigidBody is still hittable by gameplay queries - it just isn't simulated.
// An entity with neither behaves exactly as it always has. Nothing in an
// existing scene changes until a RigidBody is deliberately added to it.
//
// The three motion types are the whole vocabulary:
//   * Static    - never moves. The ground, terrain, a building. Cheapest by
//                 far: the simulation never integrates it and never tests it
//                 against other static bodies. Do not move one from a script;
//                 the broadphase caches its position on the assumption that it
//                 stays put.
//   * Kinematic - moved by YOUR code, not by forces, but it shoves dynamic
//                 bodies out of its way instead of passing through them. This
//                 is the right choice for anything under script or animation
//                 control that must still push things around - a lift, a door,
//                 or an aircraft while its flight model is still script-driven.
//                 It is immovable from the simulation's point of view: a
//                 dynamic body bouncing off it does not slow it down.
//   * Dynamic   - the real thing. Moved by gravity, by forces you apply, and
//                 by collisions with everything else.
// ============================================================================

// The numbers are written into scene files, so never renumber existing
// entries - only append.
enum class MotionType { Static = 0, Kinematic = 1, Dynamic = 2 };

class RigidBodyComponent : public Component {
public:
    const char* Name() const override { return "RigidBody"; }
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<RigidBodyComponent>(*this);
    }
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["motion"]         = static_cast<int>(motion);
        out["mass"]           = mass;
        out["friction"]       = friction;
        out["restitution"]    = restitution;
        out["linearDamping"]  = linearDamping;
        out["angularDamping"] = angularDamping;
        out["gravityFactor"]  = gravityFactor;
        out["continuous"]     = continuous;
        out["initialVelocity"] = {initialVelocity.x, initialVelocity.y,
                                  initialVelocity.z};
    }
    void Deserialize(const nlohmann::json& in) override {
        int m = in.value("motion", static_cast<int>(motion));
        // Clamp: a corrupt or newer file must not produce an enum value that
        // none of our switches handle.
        if (m < 0 || m > 2) m = 2;
        motion         = static_cast<MotionType>(m);
        mass           = in.value("mass", mass);
        friction       = in.value("friction", friction);
        restitution    = in.value("restitution", restitution);
        linearDamping  = in.value("linearDamping", linearDamping);
        angularDamping = in.value("angularDamping", angularDamping);
        gravityFactor  = in.value("gravityFactor", gravityFactor);
        continuous     = in.value("continuous", continuous);
        if (in.contains("initialVelocity"))
            initialVelocity = {in["initialVelocity"][0], in["initialVelocity"][1],
                               in["initialVelocity"][2]};
    }

    MotionType motion = MotionType::Dynamic;

    // Mass in kilograms - which is only meaningful because the engine treats
    // one world unit as one metre (see Physics.h). Mass decides how much a
    // given force accelerates the body: the same push moves a 10 kg crate ten
    // times as far as a 100 kg one. It does NOT decide how fast it falls;
    // gravity accelerates everything equally.
    float mass = 100.0f;

    // How much the surface resists sliding, roughly 0 (ice) to 1 (rubber).
    // When two bodies touch, the simulation combines both of their values.
    float friction = 0.2f;

    // How bouncy the surface is: 0 absorbs the impact completely, 1 rebounds
    // with all the speed it arrived with. Values near 1 are rarely wanted -
    // real collisions lose energy, and a perfectly elastic body never settles.
    float restitution = 0.1f;

    // Damping bleeds off motion over time, standing in for air resistance.
    // Without any, a body nudged once drifts for ever and a spinning one never
    // stops. Linear damping slows travel, angular damping slows spin; angular
    // is usually the higher of the two because unchecked tumbling looks worse
    // than unchecked drift.
    float linearDamping  = 0.05f;
    float angularDamping = 0.10f;

    // A multiplier on gravity for this body alone. 1 is normal, 0 makes it
    // weightless (useful for a projectile that should fly straight), and small
    // values read as something buoyant. Negative values make it fall upwards.
    float gravityFactor = 1.0f;

    // Sweep this body's whole path each step instead of only testing where it
    // lands ("continuous collision detection").
    //
    // The simulation normally advances in jumps and checks for overlaps at the
    // end of each one. A fast object can therefore be in front of a wall on
    // one step and behind it on the next, never overlapping it on any step, so
    // nothing is ever detected and it passes straight through. At sixty steps
    // a second an object moving 200 units a second travels more than three
    // units per step, so anything thinner than that is unreliable.
    //
    // Switching this on makes the body test the whole line it travelled. It
    // costs noticeably more, so it is off by default and belongs on small fast
    // things - bullets above all - not on everything.
    bool continuous = false;

    // The velocity the body starts with, in world units per second, applied
    // once when it enters the simulation.
    //
    // This exists because a body is not created the instant a script asks for
    // one: entities spawned during a frame join the simulation at the end of
    // it. A projectile therefore has nothing to set the velocity ON at the
    // moment it is spawned. Recording the intent here and applying it at
    // creation is what lets a bullet leave the barrel already moving, instead
    // of appearing motionless and dropping for a frame.
    Vector3 initialVelocity{0.0f, 0.0f, 0.0f};
};
} // namespace eng
