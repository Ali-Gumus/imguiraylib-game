#pragma once

#include "engine/Component.h"       // the Component base class
#include "engine/FlightModel.h"     // the JSBSim wrapper - no JSBSim types in it

#include <memory>
#include <string>

namespace eng {

// ============================================================================
// JSBSimComponent: the entity is flown by a real aerodynamic model.
// ----------------------------------------------------------------------------
// Attach this and the entity stops being moved by a script and starts being
// FLOWN. Each frame it advances a JSBSim flight dynamics model - a full
// aerodynamic simulation of a named aircraft, read from an XML description at
// runtime - and writes the result onto the entity's transform. Where it ends up
// is decided by lift, drag, thrust, inertia and the control surfaces, not by
// anything in a script.
//
// Read FlightModel.h first. It explains what a flight dynamics model is, and it
// owns the unit conversion (JSBSim works in feet and knots), the axis mapping
// (JSBSim's world is North/East/Down, this engine's is +X East, +Y Up, -Z
// North) and the fixed-rate stepping. By the time values reach this file they
// are already metres in engine axes, so this component is lifecycle and
// plumbing only.
//
// ---------------------------------------------------------------------------
// ONLY ONE THING MAY MOVE THE AIRCRAFT. That is what `enabled` is for.
// ---------------------------------------------------------------------------
// The project has TWO flight models and keeps both on purpose: this one, and
// the hand-tuned energy model in `flight_sim.lua`. They are alternatives, never
// partners. If a script is setting the transform while this component is also
// writing it, each frame ends with whichever ran last, and the result is not a
// blend of the two models - it is jitter, and an aircraft that behaves like
// neither. There is no clever arbitration here and none is wanted: untick
// `enabled` (or take the flight script off) so that exactly one of them writes.
//
// Switching does NOT have to work while playing. Stop, change it, play again.
//
// ---------------------------------------------------------------------------
// WHAT THIS COMPONENT DOES *NOT* DO
// ---------------------------------------------------------------------------
// It does not read the keyboard, and it does not touch the HUD or the engine
// sound. Those belong to a script (`flight_jsb.lua`), which reads the controls
// from input, hands them here, and publishes speed and throttle onward. Keeping
// them out means the same component flies a player aircraft, an enemy flown by
// AI, or a target drone, with no idea which it is.
//
// It also does not do COLLISION. JSBSim integrates its own state and would only
// fight being pushed, so the intended arrangement is that this component owns
// where the aircraft is, and a Kinematic RigidBody beside it carries the shape
// so the rest of the world can still hit it. A Kinematic body is moved to
// wherever its entity has been put, which is exactly what this does to it.
// ============================================================================

class JSBSimComponent : public Component {
public:
    // A FlightModel owns a whole simulation and cannot be meaningfully copied,
    // so the compiler-generated copy is deleted and Clone() below builds a
    // fresh component from the authored settings instead. This is the same
    // arrangement ModelComponent uses for its GPU resources.
    JSBSimComponent() = default;
    JSBSimComponent(const JSBSimComponent&)            = delete;
    JSBSimComponent& operator=(const JSBSimComponent&) = delete;

    const char* Name() const override { return "JSBSim"; }

    // Copy the AUTHORED settings and nothing else. The simulation itself is
    // runtime state: it is built on Play and thrown away on Stop, exactly like
    // a physics body or a particle burst, so there is nothing here to copy.
    //
    // Every field this copies must also appear in Serialize/Deserialize below,
    // and vice versa. The editor snapshots the scene by cloning it and RESTORES
    // from that snapshot when play stops, so a field Clone forgets is not
    // merely missing from a copy - it is erased from the project by pressing
    // Play and then Stop.
    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<JSBSimComponent>();
        c->enabled           = enabled;
        c->dataDir           = dataDir;
        c->aircraft          = aircraft;
        c->startAirspeedKt   = startAirspeedKt;
        c->startPathAngleDeg = startPathAngleDeg;
        c->trimOnStart       = trimOnStart;
        return c;
    }

    void Serialize(nlohmann::json& out) const override {
        out["enabled"]           = enabled;
        out["dataDir"]           = dataDir;
        out["aircraft"]          = aircraft;
        out["startAirspeedKt"]   = startAirspeedKt;
        out["startPathAngleDeg"] = startPathAngleDeg;
        out["trimOnStart"]       = trimOnStart;
    }
    void Deserialize(const nlohmann::json& in) override {
        enabled           = in.value("enabled", enabled);
        dataDir           = in.value("dataDir", dataDir);
        aircraft          = in.value("aircraft", aircraft);
        startAirspeedKt   = in.value("startAirspeedKt", startAirspeedKt);
        startPathAngleDeg = in.value("startPathAngleDeg", startPathAngleDeg);
        trimOnStart       = in.value("trimOnStart", trimOnStart);
    }

    void OnStart(Entity& owner) override;
    void OnUpdate(float dt, Entity& owner) override;
    void OnDestroy(Entity& owner) override;
    void OnInspector() override;

    // --- Authored settings ---------------------------------------------------

    // Untick to leave the entity alone entirely - no simulation is even built.
    // See the warning above: this is how the two flight models are switched.
    bool enabled = true;

    // The folder holding JSBSim's `aircraft/` and `engine/` subfolders. Relative
    // to the working directory, which the editor sets to the project root.
    std::string dataDir = "assets/jsbsim";

    // Which aircraft to fly: the name of a subfolder of `<dataDir>/aircraft`,
    // which must contain a matching `<name>.xml`. The stock JSBSim data shipped
    // with this project has "f16".
    //
    // This is read from DISK when play begins, so a name with no folder behind
    // it is a runtime problem, not a build one. It is reported in the Inspector
    // rather than being allowed to fail silently.
    std::string aircraft = "f16";

    // The flight condition the run starts in. An aircraft cannot simply be
    // placed the way a crate can: one put at 3000 m with no airspeed is not
    // "at 3000 m", it is falling. WHERE it starts and which way it faces come
    // from the entity's own transform, so those are not repeated here.
    float startAirspeedKt   = 350.0f;   // indicated airspeed, knots
    float startPathAngleDeg = 0.0f;     // climb angle; 0 is level

    // Solve for the control positions that hold that condition before the first
    // step. Without it the first second of every run is a bobble as the
    // aircraft settles, which reads as a bug in the controls rather than a
    // starting condition. Costs a few milliseconds, once.
    bool trimOnStart = true;

    // --- Runtime access, for scripts and for the Inspector readout -----------

    // Is there a working simulation right now? False while stopped, and false
    // during play if the aircraft failed to load.
    bool Ready() const { return m_fdm && m_fdm->Ready(); }

    // Why it failed to load, or "" if it did not. Shown in the Inspector.
    const std::string& Error() const { return m_error; }

    // What the aircraft is doing. Returns a zeroed state when not flying, so a
    // caller never has to check Ready() first.
    const FlightState& State() const;

    // The pilot's inputs. These PERSIST until changed - a control does not
    // spring back on its own, exactly like a real one being held - so a script
    // must set them every frame if it wants them to follow a key being held.
    void SetControls(const FlightControls& c) { m_controls = c; }
    const FlightControls& Controls() const { return m_controls; }

private:
    // Built on Play, released on Stop. Held by pointer rather than by value so
    // that an entity sitting in the editor with this component attached pays
    // nothing for a simulation it is not running.
    std::unique_ptr<FlightModel> m_fdm;

    FlightControls m_controls;
    std::string    m_error;
};

} // namespace eng
