#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/JSBSim.h"

#include <cmath>   // std::sqrt, for the speed property

namespace eng {

namespace {

// --- Two property helpers, so twenty bindings are one line each ---------------
//
// The component keeps its controls and its state in two plain structs, and Lua
// wants them as flat properties on the component itself: `jsb.throttle = 0.9`
// reads far better than fetching a control set, editing it and handing it back.
//
// Writing that out by hand is two lambdas per field, twenty times over - which
// is exactly the kind of near-identical repetition that ends with one field
// wired to another's getter. These templates take a POINTER TO MEMBER as a
// template argument, so the field is named once and the machinery around it is
// generated.

// A read/write property on one of the pilot's controls.
template <float FlightControls::*Field>
auto ControlProp() {
    return sol::property(
        [](JSBSimComponent& c) { return c.Controls().*Field; },
        [](JSBSimComponent& c, float v) { c.MutableControls().*Field = v; });
}

// A read-only property on one of the aircraft's measured values. Read-only
// because these are OUTPUTS of the simulation: assigning to an airspeed does
// not make an aircraft go faster, and a script that tried would otherwise be
// silently ignored. With safeties on, writing to one raises an error naming it.
template <float FlightState::*Field>
auto StateProp() {
    return sol::readonly_property(
        [](JSBSimComponent& c) { return c.State().*Field; });
}

} // anonymous namespace

void RegisterJSBSimBindings(sol::state& lua) {
    lua.new_usertype<JSBSimComponent>("JSBSim",
        // --- The authored settings, the same ones the Inspector shows -------
        "enabled",           &JSBSimComponent::enabled,
        "aircraft",          &JSBSimComponent::aircraft,
        "dataDir",           &JSBSimComponent::dataDir,
        "startAirspeedKt",   &JSBSimComponent::startAirspeedKt,
        "startPathAngleDeg", &JSBSimComponent::startPathAngleDeg,
        "trimOnStart",       &JSBSimComponent::trimOnStart,

        // --- The controls: what a flight script writes every frame ----------
        // These PERSIST. A control does not spring back on its own, exactly
        // like a real one being held, so a script that sets the elevator once
        // has committed the aircraft to that deflection until it says otherwise.
        "elevator", ControlProp<&FlightControls::elevator>(),
        "aileron",  ControlProp<&FlightControls::aileron>(),
        "rudder",   ControlProp<&FlightControls::rudder>(),
        "throttle", ControlProp<&FlightControls::throttle>(),
        "gear",     ControlProp<&FlightControls::gear>(),
        "brake",    ControlProp<&FlightControls::brake>(),

        // The three stick axes in one call, which is what a flight script
        // actually does each frame. Nothing the properties above cannot do;
        // it just says "fly the aircraft" in one line instead of three.
        "setStick", [](JSBSimComponent& c, float elevator, float aileron,
                       float rudder) {
            FlightControls& f = c.MutableControls();
            f.elevator = elevator;
            f.aileron  = aileron;
            f.rudder   = rudder;
        },

        // --- What the aircraft is doing -------------------------------------
        // Speeds and altitudes are offered in the units an INSTRUMENT reads
        // them in as well as in metres, because a HUD showing knots and feet is
        // not being quaint - that is what a pilot sees, and converting to
        // metres per second would make the display lie.
        "airspeedKt",  StateProp<&FlightState::airspeedKt>(),
        "altitude",    StateProp<&FlightState::altitudeM>(),   // metres
        "altitudeFt",  StateProp<&FlightState::altitudeFt>(),
        "mach",        StateProp<&FlightState::mach>(),

        // The aerodynamic state - the reason a flight model is worth having.
        // alpha is the angle between where the wing POINTS and where it is
        // GOING, and it is what lift comes from; a large one means near a
        // stall. beta is the same idea sideways. g is how many times its own
        // weight the airframe is pulling.
        "alpha",       StateProp<&FlightState::alphaDeg>(),
        "beta",        StateProp<&FlightState::betaDeg>(),
        "g",           StateProp<&FlightState::loadFactor>(),

        // Attitude in degrees. The transform carries the same information as a
        // quaternion, but a HUD wants numbers it can draw.
        "roll",        StateProp<&FlightState::rollDeg>(),
        "pitch",       StateProp<&FlightState::pitchDeg>(),
        "heading",     StateProp<&FlightState::headingDeg>(),

        // How hard the engine is actually working, 0 to 1. This LAGS the
        // throttle - a jet engine takes seconds to spool - which is why an
        // engine note should follow this rather than the lever position.
        "enginePower", StateProp<&FlightState::enginePower>(),

        // Where it is GOING, which in an aircraft is not the same as where it
        // is pointing: one in a sideslip or a stall is emphatically not moving
        // along its own nose. World axes, metres per second.
        "velocity", [](JSBSimComponent& c) {
            const FlightState& s = c.State();
            return Vector3{s.vx, s.vy, s.vz};
        },

        // Metres per second along the flight path, for anything that wants a
        // plain speed rather than an indicated airspeed - a chase camera, or a
        // lead-prediction sum.
        "speed", sol::readonly_property([](JSBSimComponent& c) {
            const FlightState& s = c.State();
            return std::sqrt(s.vx * s.vx + s.vy * s.vy + s.vz * s.vz);
        }),

        // Is there a working simulation? False while stopped, and false during
        // play if the aircraft description failed to load - so a script can say
        // something useful instead of flying an aircraft that is not there.
        "ready", sol::readonly_property(
            [](JSBSimComponent& c) { return c.Ready(); }),

        // Why it failed, or "" if it did not.
        "error", sol::readonly_property(
            [](JSBSimComponent& c) { return c.Error(); })
    );

    RegisterComponentAccess<JSBSimComponent>(lua, "JSBSim");
}

void DescribeJSBSimBindings(LuaApiRegistry& api) {
    api.Usertype("Entity", "entity")
        .Method("addComponent_JSBSim() -> JSBSim", "Add a JSBSim flight model and return it")
        .Method("getComponent_JSBSim() -> JSBSim", "Its JSBSim flight model, or nil");

    auto j = api.Usertype("JSBSim", "jsb");
    j.Prop("enabled",           "Untick and the component leaves the entity alone. ONLY ONE thing may move an aircraft");
    j.Prop("aircraft",          "Which aircraft to fly - a folder under <dataDir>/aircraft, e.g. \"f16\"");
    j.Prop("dataDir",           "Where JSBSim's aircraft/ and engine/ folders are");
    j.Prop("startAirspeedKt",   "Indicated airspeed the run begins at, knots");
    j.Prop("startPathAngleDeg", "Climb angle the run begins at, degrees. 0 is level");
    j.Prop("trimOnStart",       "Solve for the controls that hold the starting condition");

    j.Prop("elevator", "-1 to 1. NEGATIVE is nose UP. Persists until changed");
    j.Prop("aileron",  "-1 to 1. Positive rolls right");
    j.Prop("rudder",   "-1 to 1. Positive yaws right");
    j.Prop("throttle", "0 to 1. The top of the range lights the afterburner");
    j.Prop("gear",     "0 up, 1 down. Extended gear is a lot of drag");
    j.Prop("brake",    "0 to 1, wheel brakes. Only means anything on the ground");
    j.Method("setStick(elevator, aileron, rudder)", "All three stick axes at once");

    j.Prop("airspeedKt",  "INDICATED airspeed in knots - what a gauge shows");
    j.Prop("altitude",    "Metres above sea level");
    j.Prop("altitudeFt",  "Feet above sea level - what an altimeter shows");
    j.Prop("mach",        "Speed as a fraction of the local speed of sound");
    j.Prop("alpha",       "Angle of attack, degrees. Lift comes from this; a big one means near a stall");
    j.Prop("beta",        "Sideslip, degrees. Non-zero means flying crabbed");
    j.Prop("g",           "Load factor. 1 in level flight, negative when pushing over");
    j.Prop("roll",        "Degrees, positive right wing down");
    j.Prop("pitch",       "Degrees, positive nose up");
    j.Prop("heading",     "Degrees, 0 north and 90 east");
    j.Prop("enginePower", "0 to 1, and it LAGS the throttle. Drive the engine note from this");
    j.Method("velocity() -> Vector3", "Where it is GOING, world axes, m/s - not the same as where it points");
    j.Prop("speed",       "Metres per second along the flight path");
    j.Prop("ready",       "Is there a working simulation? False while stopped, or if the aircraft failed to load");
    j.Prop("error",       "Why it failed to load, or \"\"");
}

} // namespace eng
