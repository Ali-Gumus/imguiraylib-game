#pragma once

// ============================================================================
// FlightModel: a real aerodynamic flight simulation, built on JSBSim.
// ----------------------------------------------------------------------------
// Everything that has flown in this engine so far flies by an ENERGY MODEL:
// flight_sim.lua carries a velocity vector, adds a thrust along the nose, takes
// a drag off it proportional to speed squared, and turns the aircraft by simply
// rotating it at a fixed number of degrees per second. That is easy to reason
// about and easy to tune, but the aircraft is not obeying any aerodynamics -
// it turns just as willingly at 80 knots as at 500, it cannot stall, and the
// numbers in it mean whatever felt right rather than anything measurable.
//
// A FLIGHT DYNAMICS MODEL (FDM) inverts that the same way a physics engine
// inverts hand-moved objects. You describe the AIRCRAFT once - its wing area,
// its mass and inertia, how much lift its wing makes at each angle of attack,
// how much moment its elevator produces, what its engine's thrust curve looks
// like - and then you only ever move the CONTROLS. Stalls, spins, the way a
// turn bleeds speed, the way a heavy aircraft mushes: all of that falls out of
// the aerodynamic description instead of being written case by case.
//
// The vocabulary, since it is used throughout:
//   * FDM            - the whole simulation of one aircraft.
//   * ANGLE OF ATTACK (alpha) - the angle between where the wing is POINTING
//                      and where it is actually GOING. Lift comes from this
//                      angle, not from the nose attitude, which is why an
//                      aircraft can be nose-high and still descending.
//   * SIDESLIP (beta) - the same idea sideways: the aircraft flying slightly
//                      crabbed rather than straight into the airflow.
//   * LOAD FACTOR    - how many times its own weight the airframe is pulling.
//                      1 g in level flight, 9 g in a hard turn.
//   * TRIM           - solving for the control positions that hold a steady
//                      condition, so a run does not begin with the aircraft
//                      already tumbling.
//   * KIAS           - indicated airspeed in knots: what an airspeed gauge
//                      shows, which falls with altitude even when the true
//                      speed through the air does not.
//
// ---------------------------------------------------------------------------
// THIS HEADER DELIBERATELY EXPOSES NO JSBSim TYPES, AND NO raylib ONES EITHER.
// ---------------------------------------------------------------------------
// The .cpp beside it is the ONLY file in the project that includes JSBSim, in
// exactly the way Physics.cpp is the only file that includes Jolt, and
// FileDialog.cpp the only one that includes <windows.h>. The reason is the same
// and it is not theoretical: JSBSim brings a namespace full of very short names
// (`FGColumnVector3`, `Element`, `Table`), a global `SGPath`, and its own math
// types, into a project whose rendering library already occupies the global
// namespace with `Color`, `Ray`, `Matrix` and `Plane`. Any header that let
// those meet would force every future file to fight the collision.
//
// So the structs below are plain floats. The caller converts them to raylib's
// Vector3 and Quaternion on its side of the wall; this file never sees one.
//
// ---------------------------------------------------------------------------
// UNITS AND AXES ARE CONVERTED HERE, ONCE.
// ---------------------------------------------------------------------------
// JSBSim is an aerospace tool and works in FEET, KNOTS and SLUGS, with its
// world axes pointing NORTH / EAST / DOWN and its aircraft axes pointing
// NOSE / RIGHT WING / BELLY. This engine works in METRES with +Y up and an
// object facing its own -Z. Neither convention is wrong and neither is going to
// change, so the conversion has to live somewhere - and it lives HERE, so that
// every caller sees metres, and a unit mistake can only ever be in one file.
//
// The axis mapping, written out because it is impossible to guess:
//     engine +X  =  EAST        engine +Y  =  UP        engine -Z  =  NORTH
// which is a right-handed set, matching the engine's own. Note the world is
// FLAT: JSBSim simulates a round rotating earth, and this takes the aircraft's
// north/east offset from where it started and treats that as a plane. Over the
// tens of kilometres this game covers, the difference is far below a pixel.
// ============================================================================

#include <memory>   // std::unique_ptr, for the pointer that hides the FDM
#include <string>   // the error text, and the aircraft name

namespace eng {

// --- What comes OUT of the simulation ---------------------------------------
// Read-only: this is what the aircraft is doing, sampled after the last step.
// Everything is in engine units (metres, m/s, engine axes) EXCEPT the few
// fields whose aviation unit is the whole point of showing them - an airspeed
// tape reads knots, an altimeter reads feet, and converting those to metres per
// second would make the instruments lie about what a pilot would see.
struct FlightState {
    // Where it is, in world space, metres. Relative to the world position the
    // run was started from - see FlightStart below.
    float x = 0.0f, y = 0.0f, z = 0.0f;

    // Which way it is pointing, as a quaternion in the engine's own convention
    // (an unrotated aircraft faces -Z, +Y is up). Assign straight into a
    // Transform3D's rotation.
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;

    // How fast it is going, world axes, metres per second. This is the whole
    // velocity vector, not just the speed along the nose: an aircraft in a
    // sideslip or a stall is emphatically not going where it points.
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;

    // The instrument readings. Kept in their aviation units on purpose.
    float airspeedKt = 0.0f;   // INDICATED airspeed, knots - what a gauge shows
    float altitudeFt = 0.0f;   // above sea level, feet - what an altimeter shows
    float altitudeM  = 0.0f;   // the same altitude in metres, for game logic
    float mach       = 0.0f;   // speed as a fraction of the local speed of sound

    // Height above the GROUND, metres - which over a landscape is a completely
    // different number from height above sea level, and is the one that decides
    // whether the aircraft is about to hit something. Zero or below means it is
    // touching. Only meaningful once SetTerrainElevation has been told where the
    // ground is; without that the simulation believes the world is all sea.
    float altitudeAglM = 0.0f;

    // The aerodynamic state. These are what make an FDM worth having: they say
    // HOW the aircraft is flying, not merely where it is.
    float alphaDeg     = 0.0f;   // angle of attack - large means near a stall
    float betaDeg      = 0.0f;   // sideslip - non-zero means flying crabbed
    float loadFactor   = 1.0f;   // g. 1 in level flight, negative when pushing over

    // The attitude in degrees, which is what a HUD wants to draw and what is
    // far easier to read in a debug panel than four quaternion components.
    float rollDeg    = 0.0f;   // positive = right wing down
    float pitchDeg   = 0.0f;   // positive = nose up
    float headingDeg = 0.0f;   // 0 = north, 90 = east, 0..360

    // How hard the engine is actually working, 0..1. This LAGS the throttle
    // command - a jet engine takes seconds to spool - which is exactly why the
    // engine note should follow this rather than the stick position.
    float enginePower = 0.0f;

    // How far into AFTERBURNER the engine is, 0 (dry) to 1 (full reheat).
    //
    // An afterburner is raw fuel sprayed into the exhaust behind the turbine
    // and lit. It buys a large amount of extra thrust for an enormous amount of
    // extra fuel - on this engine, 17,800 lb of thrust dry against 29,000 with
    // reheat, for nearly three times the consumption - and it is the thing that
    // produces the visible flame out of the tailpipe.
    //
    // WHERE THE NUMBER COMES FROM, because it is not where you would guess.
    // JSBSim's turbine offers three ways of triggering reheat, and this
    // aircraft uses the one where the THROTTLE RANGE IS EXTENDED: the engine's
    // own throttle position runs 0 to 2, everything above 1 is afterburner, and
    // the aircraft's flight control system is what stretches the pilot's 0-to-1
    // lever onto it. The F-16 doubles it, so reheat begins at the HALFWAY point
    // of the lever and is fully lit at the top.
    //
    // Reading the engine's position rather than the lever means this stays
    // right for an aircraft whose control system stretches it differently, and
    // reads a harmless 0 for one that has no afterburner at all.
    float afterburner = 0.0f;

    // FUEL, which in a real aircraft is finite and in a flight model is finite
    // too. This is not a detail: an aircraft that runs its tanks dry loses
    // thrust entirely, and the symptom is an engine that seems to cut out while
    // the throttle does nothing and the aircraft will not accelerate even in a
    // dive. That reads as a bug rather than as having flown too long on
    // afterburner, which is exactly why it is worth being able to SEE.
    //
    // `fuelLb` is what remains, in pounds (JSBSim's own unit for it).
    // `fuelFraction` is that as a share of what the run STARTED with, so a gauge
    // can be drawn without the caller knowing the aircraft's tank capacity.
    float fuelLb       = 0.0f;
    float fuelFraction = 1.0f;
};

// --- What goes IN ------------------------------------------------------------
// The pilot's four inputs, and nothing else. Every one is normalised, so a
// script never has to know what deflection in degrees a real F-16's elevator
// reaches: full back stick is -1 whatever aircraft is loaded.
struct FlightControls {
    // -1 .. +1. NEGATIVE is nose UP, which is not a typo: an elevator makes the
    // nose rise by deflecting UPWARD, which is a negative surface angle in the
    // aerospace sign convention JSBSim uses. Getting this backwards produces an
    // aircraft that flies perfectly and inverts every input, so it is called
    // out here rather than left to be discovered in the air.
    float elevator = 0.0f;

    // -1 .. +1. Positive rolls RIGHT (right wing down).
    float aileron = 0.0f;

    // -1 .. +1. WHICH WAY IT YAWS DEPENDS ON THE AIRCRAFT, so this one cannot
    // be documented as a direction the way the two above can.
    //
    // On a simple aeroplane the command is a surface deflection and positive
    // yaws right. On a fly-by-wire one it is a REQUEST that the flight control
    // system interprets: the stock F-16 sums it into a yaw-rate damper which
    // then drives the surface to null the resulting error, which both inverts
    // the sense and very nearly cancels it. Measured on that model, three
    // seconds of full rudder from level flight turns the aircraft 1.3 degrees.
    //
    // That is realistic - a fighter's rudder is for crosswind landings and
    // departure recovery, not for steering - but it means any script mapping a
    // key to this must check which way its aircraft actually goes.
    float rudder = 0.0f;

    // 0 .. 1. How far the throttle lever is forward. On an afterburning engine
    // like the F-16's the top of this range lights the burner.
    float throttle = 0.0f;

    // 0 = up, 1 = down. Retracted by default: this game starts in the air, and
    // extended gear on a fighter is a large amount of drag for no reason.
    float gear = 0.0f;

    // 0 .. 1, wheel brakes. Only means anything on the ground.
    float brake = 0.0f;
};

// --- Where a run BEGINS -------------------------------------------------------
// A flight model cannot simply be dropped at a position the way a rigid body
// can: it has to be given a whole flight CONDITION, because an aircraft placed
// at 3000 m with no airspeed is not "at 3000 m", it is falling.
struct FlightStart {
    // The world position, in metres, that the aircraft begins at. This also
    // becomes the origin the simulation's north/east offsets are measured from,
    // so the game world's coordinates and JSBSim's stay in step for the run.
    float x = 0.0f, y = 3000.0f, z = 0.0f;

    // Which way it is pointing when the run starts, degrees, 0 = north.
    float headingDeg = 0.0f;

    // How fast it is going, knots indicated. Must be enough for the wing to fly
    // or the run opens with a stall.
    float airspeedKt = 350.0f;

    // The initial climb or dive angle, degrees, positive up. Zero is level.
    float pathAngleDeg = 0.0f;

    // How high the ground is beneath the starting point, metres above sea
    // level. Set alongside the position, because "3000 m up" over a 700 m
    // mountain range is 2300 m of air, and the simulation has to agree with the
    // landscape from the first step rather than from the first update.
    float terrainElevationM = 0.0f;

    // How much fuel to begin with, in pounds. ZERO MEANS "WHATEVER THE AIRCRAFT
    // DESCRIPTION SAYS", which is the sensible default: the stock F-16 is
    // authored with 3000 lb in its two internal tanks.
    //
    // Set it to change how long the aircraft can fly without editing the
    // aircraft's XML - which would change it for every aircraft of that type at
    // once, and is a fiddly place to keep a gameplay number.
    //
    // The amount is spread across the tanks IN THE PROPORTIONS THE AIRCRAFT WAS
    // AUTHORED WITH, rather than poured into the first one. Two reasons, and
    // the second is easy to overlook:
    //   * fuel is mass, and where it sits moves the centre of gravity;
    //   * a tank the aircraft carries EMPTY stays empty. The stock F-16 has two
    //     external drop tanks at zero, and filling those would hang several
    //     tonnes on the airframe - it would still fly, it would turn and
    //     accelerate noticeably worse, and nothing would say why.
    //
    // Each tank still has a capacity, so asking for more than the filled tanks
    // can hold is clamped rather than honoured. State().fuelLb after Reset is
    // what was actually loaded.
    float fuelLb = 0.0f;

    // Solve for the controls that hold this condition before the first step, so
    // the run does not begin with the aircraft already pitching. Worth leaving
    // on; it costs a few milliseconds once.
    bool trim = true;
};

// --- The simulation itself -----------------------------------------------------
// One instance is one aircraft. Copying is disabled because an FDM owns a large
// amount of internal state that has no meaningful copy - use a reference or move
// it, exactly as with any other simulation object.
class FlightModel {
public:
    FlightModel();
    ~FlightModel();

    FlightModel(const FlightModel&)            = delete;
    FlightModel& operator=(const FlightModel&) = delete;
    FlightModel(FlightModel&&) noexcept;
    FlightModel& operator=(FlightModel&&) noexcept;

    // Read an aircraft description off disk and build a simulation of it.
    //
    // `dataDir` is the folder holding `aircraft/` and `engine/` - in this
    // project, "assets/jsbsim". `aircraft` is the name of a subfolder of
    // `aircraft/`, e.g. "f16", which must contain a matching `<name>.xml`.
    //
    // Returns false and fills Error() if anything is missing or malformed.
    // Failure is NOT fatal by design: the entity simply does not fly, exactly
    // as a missing model file draws a fallback rather than crashing the editor.
    // The aircraft data is read at RUNTIME, so a typo here is a runtime problem
    // that must be reported rather than a build error that cannot happen.
    bool Load(const std::string& dataDir, const std::string& aircraft);

    // Is there a working simulation? Every call below is safe either way, and
    // does nothing when there is not.
    bool Ready() const;

    // Why Load failed, or "" if it did not.
    const std::string& Error() const;

    // Put the aircraft into a starting flight condition. Safe to call again to
    // restart a run without reloading the aircraft description, which is the
    // point: parsing the XML is slow, resetting is not.
    void Reset(const FlightStart& start);

    // Set the pilot's inputs. These persist until changed - a control does not
    // spring back on its own, exactly like a real one being held.
    void SetControls(const FlightControls& controls);
    const FlightControls& Controls() const;

    // Tell the simulation how high the ground is beneath the aircraft, in
    // metres above sea level.
    //
    // THIS MATTERS MORE THAN IT LOOKS. A flight dynamics model has a ground in
    // it: undercarriage that touches down, wheels that take weight, an airframe
    // that strikes. But it has no idea what shape the landscape is, so unless
    // it is told, it believes the entire world is a flat sea at zero - and an
    // aircraft flying at 300 m over a 700 m mountain is, as far as the
    // simulation is concerned, 300 m up in clear air.
    //
    // The value is only correct beneath the aircraft's CURRENT position, so
    // over a landscape this has to be updated as it moves, every frame.
    void SetTerrainElevation(float metresAboveSeaLevel);

    // Keep the tanks topped up, so the aircraft never runs out of fuel.
    //
    // A flight model burns fuel, and when it is gone the engine stops: no
    // thrust, at any throttle setting, for the rest of the run. That is correct
    // and it is also brutal - the stock F-16 is authored with its tanks 43%
    // full, which at combat power is under FOUR MINUTES. Worse, the symptom
    // gives no clue as to the cause: the throttle simply stops doing anything
    // and the aircraft will not accelerate even pointed at the ground.
    //
    // Implemented with the simulation's own refuelling flag, which tops the
    // tanks up at 6000 lb per minute against a maximum burn of about 990, so
    // they stay full whatever the throttle is doing. Held on rather than set
    // once, so it can be switched mid-run.
    void SetUnlimitedFuel(bool on);

    // Advance by `dt` seconds of game time.
    //
    // JSBSim runs at a FIXED RATE OF ITS OWN (120 Hz for the stock aircraft),
    // because integrating an aerodynamic model is only stable at a constant
    // step - the same reason the rigid-body world in Physics.cpp runs at a fixed
    // 60 Hz. The frame time it is handed is anything but constant, so this banks
    // the real time that has passed and spends it in whole steps, carrying the
    // remainder into the next frame. Nothing is lost or double-counted, and the
    // caller never has to know what rate the simulation wants.
    void Advance(float dt);

    // What the aircraft is doing, as of the last step. Cheap: the values are
    // sampled once per Advance, not read from the property tree on demand.
    const FlightState& State() const;

    // The simulation's own step size in seconds, and how many steps the last
    // Advance actually ran. The second is a diagnostic worth having: a frame
    // that ran zero steps or hit the ceiling is the first thing to check when
    // the aircraft feels sluggish or jumpy.
    float FixedStepSeconds() const;
    int   LastStepCount() const;

    // Time that has been handed to Advance but NOT yet simulated, in seconds.
    // Always less than one step.
    //
    // WHAT IT IS FOR, and it matters more than it sounds. The simulation only
    // ever advances in whole steps, so after a frame there is almost always a
    // leftover: the state describes the aircraft at a moment slightly BEFORE
    // now. Drawing it there makes the aircraft move in lumps rather than
    // smoothly - measured, a 20 ms frame and a 14 ms frame both moved it
    // exactly 5.28 m, because both spent the same two steps.
    //
    // At a steady 60 fps this is invisible, because a frame is exactly two of
    // the F-16's 120 Hz steps and the leftover is always zero. It only appears
    // when the frame rate moves - and then the aircraft judders, and anything
    // positioned by predicting where it will be lands somewhere else.
    //
    // Multiplying the velocity by this and adding it to the position carries
    // the state forward to the actual present, which is the standard cure for
    // watching a fixed-rate simulation through variable frames.
    float PendingSeconds() const;

private:
    // The PIMPL ("pointer to implementation") idiom, and the thing that makes
    // the quarantine at the top of this file possible. The class that actually
    // holds the FDM is DECLARED here and DEFINED only inside the .cpp, so this
    // header never has to name a JSBSim type - not even to say how big one is.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace eng
