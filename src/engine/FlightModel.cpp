// ============================================================================
// FlightModel.cpp - THE ONLY FILE IN THIS PROJECT THAT INCLUDES JSBSim.
// ----------------------------------------------------------------------------
// Read the header first: it explains what a flight dynamics model is, why this
// wall exists, and what the unit and axis conventions on either side of it are.
// This file is the wall itself.
//
// Two rules are kept absolutely, and both are worth stating before the code:
//
//   1. NO raylib HEADER IS INCLUDED HERE. Not raylib.h, not raymath.h. raylib
//      puts `Color`, `Ray`, `Matrix`, `Quaternion` and more in the GLOBAL
//      namespace, and JSBSim drags in its own aerospace maths types plus a
//      global `SGPath`. Keeping them apart is free here and expensive later,
//      so the small amount of quaternion arithmetic this file needs is written
//      out by hand below rather than borrowed from raymath.
//
//   2. `using namespace JSBSim` IS NEVER WRITTEN. Every JSBSim name is spelled
//      out in full, the same discipline Physics.cpp keeps with `JPH::`.
//
// The simulation is driven almost entirely through JSBSim's PROPERTY TREE - a
// single string-keyed table of every value inside the simulation, which is how
// JSBSim's own config files, autopilots and scripts talk to it. Using it rather
// than the C++ accessors is a deliberate choice: it means this file names
// exactly one JSBSim type (FGFDMExec) instead of a dozen, which is what keeps
// the quarantine cheap to maintain. The cost is that a mistyped property name
// is a runtime problem rather than a compile error, so every name used here is
// listed in one block at the top where it can be checked against JSBSim's
// documentation in one reading.
// ============================================================================

#include "engine/FlightModel.h"

#include <cmath>      // std::sin, std::cos, std::sqrt, std::atan2
#include <string>

// The one JSBSim header. It pulls in the property manager and the model tree
// behind it, which is why nothing else in the engine may include it.
#include "FGFDMExec.h"

namespace eng {

namespace {

// --- Unit conversions ---------------------------------------------------------
// JSBSim is an aerospace tool and speaks feet and knots; the engine speaks
// metres. These are the exact defined conversions, not approximations: the
// international foot has been exactly 0.3048 m since 1959.
constexpr float kFeetToMetres = 0.3048f;
constexpr float kDegToRad     = 3.14159265358979323846f / 180.0f;
constexpr float kRadToDeg     = 180.0f / 3.14159265358979323846f;

// --- The property names, all in one place -------------------------------------
// JSBSim's property tree is addressed by string, so these cannot be checked by
// the compiler. Gathering them here means a name can be verified against the
// JSBSim reference once, in one place, instead of being hunted through the code.
//
// Note the ones that read POSITION. JSBSim simulates a round, rotating earth
// and stores position as latitude and longitude; `from-start-neu-*` is the
// offset in feet North, East and Up from wherever the run was initialised,
// which is exactly the flat-world quantity this engine wants.
constexpr const char* kPosNorthFt = "position/from-start-neu-n-ft";
constexpr const char* kPosEastFt  = "position/from-start-neu-e-ft";
constexpr const char* kAltSlFt    = "position/h-sl-ft";

constexpr const char* kVelNorthFps = "velocities/v-north-fps";
constexpr const char* kVelEastFps  = "velocities/v-east-fps";
constexpr const char* kVelDownFps  = "velocities/v-down-fps";

constexpr const char* kPhiDeg   = "attitude/phi-deg";    // roll
constexpr const char* kThetaDeg = "attitude/theta-deg";  // pitch
constexpr const char* kPsiDeg   = "attitude/psi-deg";    // true heading

constexpr const char* kVcKts      = "velocities/vc-kts";   // indicated airspeed
constexpr const char* kMach       = "velocities/mach";
constexpr const char* kAlphaDeg   = "aero/alpha-deg";
constexpr const char* kBetaDeg    = "aero/beta-deg";
constexpr const char* kLoadFactor = "forces/load-factor";
constexpr const char* kEngineN2   = "propulsion/engine[0]/n2";

constexpr const char* kCmdElevator = "fcs/elevator-cmd-norm";
constexpr const char* kCmdAileron  = "fcs/aileron-cmd-norm";
constexpr const char* kCmdRudder   = "fcs/rudder-cmd-norm";
constexpr const char* kCmdThrottle = "fcs/throttle-cmd-norm[0]";
constexpr const char* kCmdGear     = "gear/gear-cmd-norm";
constexpr const char* kCmdBrakeL   = "fcs/left-brake-cmd-norm";
constexpr const char* kCmdBrakeR   = "fcs/right-brake-cmd-norm";

// Setting this to -1 starts EVERY engine on the aircraft, already spooled up.
// Without it the F-16 is loaded cold and dark: the throttle does nothing, the
// aircraft glides, and there is no error anywhere to say why.
constexpr const char* kSetRunning = "propulsion/set-running";

// The initial-condition branch. Writing to these does not move the aircraft;
// they describe the condition that the next RunIC() will build.
constexpr const char* kIcAltFt      = "ic/h-sl-ft";
constexpr const char* kIcVcKts      = "ic/vc-kts";
constexpr const char* kIcHeadingDeg = "ic/psi-true-deg";
constexpr const char* kIcGammaDeg   = "ic/gamma-deg";   // flight path angle
constexpr const char* kIcPhiDeg     = "ic/phi-deg";
constexpr const char* kIcLatDeg     = "ic/lat-gc-deg";
constexpr const char* kIcLonDeg     = "ic/long-gc-deg";

// The latitude and longitude the game world sits at. It has to be SOMEWHERE,
// because JSBSim's atmosphere and gravity are functions of position on a real
// earth - so this picks a point and stays there. The choice is arbitrary but it
// must be constant: moving it would change the air density and therefore every
// number the aircraft produces. Away from the poles so that a degree of
// longitude is a sane distance, and away from the date line so no offset the
// game produces can wrap it.
constexpr double kWorldLatDeg = 40.0;
constexpr double kWorldLonDeg = 30.0;

// Trim mode 0 is JSBSim's `tLongitudinal`: it solves for the elevator, angle of
// attack and thrust that hold a steady flight path. That is the right one for
// an aircraft starting in mid-air. (Mode 2, `tGround`, is for a runway start,
// and would fail outright at altitude.)
constexpr int kTrimLongitudinal = 0;

// The most steps one frame may run. A frame that arrives after a long stall -
// loading a scene, dragging the window - would otherwise ask for hundreds of
// steps, which takes long enough to cause an even longer frame, which asks for
// even more steps: the classic "spiral of death". The ceiling breaks it by
// letting simulated time fall behind real time instead. At 120 Hz, 12 steps is
// a tenth of a second, comfortably more than any real frame needs.
constexpr int kMaxStepsPerFrame = 12;

// --- Quaternion arithmetic, written out ---------------------------------------
// Only two operations are needed, and doing them here rather than including
// raymath is what keeps raylib out of this file (see the note at the top).
//
// A quaternion is a compact, singularity-free way to store an orientation: four
// numbers encoding "turn by this angle about this axis". Stored (x, y, z, w)
// with w last, which is raylib's layout, so the four floats can be assigned
// straight across without reordering.
struct Quat { float x, y, z, w; };

// The rotation of `angleRad` about a unit axis. The half-angle is not a
// mistake: a quaternion applies its rotation twice when it is used to rotate a
// vector, so it stores half of the turn it represents.
Quat QuatFromAxisAngle(float ax, float ay, float az, float angleRad) {
    const float half = angleRad * 0.5f;
    const float s    = std::sin(half);
    return { ax * s, ay * s, az * s, std::cos(half) };
}

// Compose two rotations. Order matters and is not symmetric: `a * b` means
// "do b's rotation in a's frame", so composing left-to-right walks OUTWARD from
// the world toward the object's own axes.
Quat QuatMultiply(const Quat& a, const Quat& b) {
    return {
        a.x * b.w + a.w * b.x + a.y * b.z - a.z * b.y,
        a.y * b.w + a.w * b.y + a.z * b.x - a.x * b.z,
        a.z * b.w + a.w * b.z + a.x * b.y - a.y * b.x,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

// --- The attitude conversion, which is the subtle part -------------------------
// JSBSim reports attitude as three Euler angles in the aerospace convention:
//   phi   - roll,    positive RIGHT WING DOWN
//   theta - pitch,   positive NOSE UP
//   psi   - heading, positive CLOCKWISE FROM NORTH (0 north, 90 east)
// applied in the order heading, then pitch, then roll, each about the axes the
// previous rotation left behind ("intrinsic" rotations).
//
// The engine's axes are +X right, +Y up, -Z forward, and its world is mapped
// so that -Z is North and +X is East (see the header). Working out what each
// aerospace angle becomes in those axes is done once, here:
//
//   HEADING. At psi = 0 the nose points North, which is -Z: the identity. At
//   psi = 90 it must point East, which is +X. Rotating -Z about +Y by an angle
//   `a` gives (-sin a, 0, -cos a), which reaches +X at a = -90. So the engine
//   rotation is about +Y by MINUS psi. The sign flip is not arbitrary: compass
//   bearings run clockwise seen from above, and a right-handed rotation about
//   an up axis runs anticlockwise.
//
//   PITCH. Rotating -Z about the right wing (+X) by `b` gives (0, sin b, -cos b),
//   so a positive angle already raises the nose. theta passes through unchanged.
//
//   ROLL. Rotating +X about +Z by `c` gives (cos c, sin c, 0), which raises the
//   right wing - the opposite of what positive phi means. So the engine rotation
//   is about +Z by MINUS phi. (Equivalently: about the forward axis -Z by plus
//   phi, which is the same rotation said the other way round.)
Quat AttitudeToEngineQuat(float phiDeg, float thetaDeg, float psiDeg) {
    const Quat yaw   = QuatFromAxisAngle(0.0f, 1.0f, 0.0f, -psiDeg   * kDegToRad);
    const Quat pitch = QuatFromAxisAngle(1.0f, 0.0f, 0.0f,  thetaDeg * kDegToRad);
    const Quat roll  = QuatFromAxisAngle(0.0f, 0.0f, 1.0f, -phiDeg   * kDegToRad);

    // Outward-in: heading orients the aircraft in the world, pitch then acts
    // about the wing line that heading produced, and roll about the nose that
    // pitch produced. Reversing this order gives an aircraft that rolls about
    // a world axis instead of its own, which looks correct in level flight and
    // wrong in every turn.
    return QuatMultiply(QuatMultiply(yaw, pitch), roll);
}

// Clamp, for the normalised control inputs. A stick pushed past its stop is a
// script bug, not a manoeuvre, and JSBSim would happily deflect a surface to an
// angle the real aircraft cannot reach.
float Clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // anonymous namespace

// ============================================================================
// The implementation the header promised but would not describe.
// ============================================================================
struct FlightModel::Impl {
    // JSBSim writes a banner and a running commentary to the console, and it
    // does so from FGFDMExec's own CONSTRUCTOR - so switching it off after
    // building one is already too late. The verbosity is a single static shared
    // by everything in the library, and this tiny member exists only to set it.
    //
    // It is declared BEFORE `fdm` on purpose: members are constructed in
    // declaration order, so this one runs first. Moving it below `fdm` would
    // compile, do nothing visible, and bring the banner back - which is the
    // kind of silent failure worth a comment.
    struct Quieten {
        Quieten() { JSBSim::FGJSBBase::debug_lvl = 0; }
    } quieten;

    // JSBSim's top-level object: it owns the aircraft, the atmosphere, the
    // propulsion and the integrator, and stepping it steps all of them.
    JSBSim::FGFDMExec fdm;

    bool           ready = false;   // did an aircraft load?
    std::string    error;           // why not, if not
    FlightControls controls;        // the last inputs given, held until changed
    FlightState    state;           // the last sample taken, in engine units

    // Where in the GAME world this run began, metres. JSBSim reports its
    // position as an offset from where it was initialised, so this is added
    // back on to place the aircraft in the scene.
    float originX = 0.0f, originZ = 0.0f;

    // Unspent real time, seconds. See Advance().
    float accumulator = 0.0f;
    int   lastSteps   = 0;

    // Read the property tree. Returns 0 for a name the aircraft does not have,
    // which is the behaviour wanted: an aircraft with no afterburner should
    // report no afterburner rather than fail.
    float Get(const char* name) const {
        return static_cast<float>(
            const_cast<JSBSim::FGFDMExec&>(fdm).GetPropertyValue(name));
    }
    void Set(const char* name, double value) { fdm.SetPropertyValue(name, value); }

    // Copy the simulation's state into `state`, converting units and axes once.
    // Called after every Advance rather than on demand, so that reading the
    // state a dozen times in a frame costs a dozen struct reads instead of a
    // dozen walks of a string-keyed tree.
    void Sample();

    // Push `controls` into the property tree. Separate from Sample because the
    // controls must be written BEFORE a step and the state read AFTER it.
    void PushControls();
};

void FlightModel::Impl::Sample() {
    // --- Position. North/East/Up feet from the start point, into engine axes.
    const float northM = Get(kPosNorthFt) * kFeetToMetres;
    const float eastM  = Get(kPosEastFt)  * kFeetToMetres;
    const float altFt  = Get(kAltSlFt);

    state.x = originX + eastM;
    state.y = altFt * kFeetToMetres;    // sea level is y = 0 in this game
    state.z = originZ - northM;         // -Z is North, so North is -Z

    state.altitudeFt = altFt;
    state.altitudeM  = state.y;

    // --- Velocity. Same mapping; "down" becomes "up" by negation.
    state.vx =  Get(kVelEastFps)  * kFeetToMetres;
    state.vy = -Get(kVelDownFps)  * kFeetToMetres;
    state.vz = -Get(kVelNorthFps) * kFeetToMetres;

    // --- Attitude, both as angles for instruments and as a quaternion for the
    // transform. Reading both from the same sample keeps them from disagreeing
    // by one frame, which is exactly the kind of drift that makes a HUD swim.
    state.rollDeg    = Get(kPhiDeg);
    state.pitchDeg   = Get(kThetaDeg);
    state.headingDeg = Get(kPsiDeg);

    const Quat q = AttitudeToEngineQuat(state.rollDeg, state.pitchDeg,
                                        state.headingDeg);
    state.qx = q.x;  state.qy = q.y;  state.qz = q.z;  state.qw = q.w;

    // --- The aerodynamic state, in the units instruments read them in.
    state.airspeedKt = Get(kVcKts);
    state.mach       = Get(kMach);
    state.alphaDeg   = Get(kAlphaDeg);
    state.betaDeg    = Get(kBetaDeg);
    state.loadFactor = Get(kLoadFactor);

    // N2 is the speed of the engine's high-pressure spool as a percentage of
    // its maximum. It is used rather than the throttle position because it is
    // what actually LAGS: a jet engine takes seconds to spool up, and an engine
    // note that follows the lever instead of the spool sounds like a switch.
    state.enginePower = Clamp(Get(kEngineN2) * 0.01f, 0.0f, 1.0f);
}

void FlightModel::Impl::PushControls() {
    Set(kCmdElevator, Clamp(controls.elevator, -1.0f, 1.0f));
    Set(kCmdAileron,  Clamp(controls.aileron,  -1.0f, 1.0f));
    Set(kCmdRudder,   Clamp(controls.rudder,   -1.0f, 1.0f));
    Set(kCmdThrottle, Clamp(controls.throttle,  0.0f, 1.0f));
    Set(kCmdGear,     Clamp(controls.gear,      0.0f, 1.0f));

    // Both brakes together: this engine has no separate left and right pedal,
    // and differential braking is a taxiing control the game has no use for.
    const float brake = Clamp(controls.brake, 0.0f, 1.0f);
    Set(kCmdBrakeL, brake);
    Set(kCmdBrakeR, brake);
}

// --- Construction --------------------------------------------------------------

FlightModel::FlightModel() : m_impl(std::make_unique<Impl>()) {}

// The destructor and the move operations MUST be defined here rather than
// defaulted in the header. `std::unique_ptr<Impl>` needs to see Impl's full
// definition to destroy it, and that definition exists only in this file - the
// whole point of the pimpl. Defaulting them in the header would fail to
// compile at every call site instead.
FlightModel::~FlightModel() = default;
FlightModel::FlightModel(FlightModel&&) noexcept = default;
FlightModel& FlightModel::operator=(FlightModel&&) noexcept = default;

bool FlightModel::Ready() const { return m_impl->ready; }
const std::string& FlightModel::Error() const { return m_impl->error; }
const FlightState& FlightModel::State() const { return m_impl->state; }
const FlightControls& FlightModel::Controls() const { return m_impl->controls; }
int FlightModel::LastStepCount() const { return m_impl->lastSteps; }

float FlightModel::FixedStepSeconds() const {
    return static_cast<float>(m_impl->fdm.GetDeltaT());
}

// --- Loading ------------------------------------------------------------------

bool FlightModel::Load(const std::string& dataDir, const std::string& aircraft) {
    Impl& im = *m_impl;
    im.ready = false;
    im.error.clear();

    // JSBSim reads its aircraft from DISK at runtime, so these four paths are
    // as load-bearing as any code here.
    //
    // `SGPath` is JSBSim's own path type, and note that it is NOT inside the
    // JSBSim namespace - it is a bare global name inherited from the SimGear
    // library JSBSim borrows it from. It is exactly the kind of name this file
    // exists to contain, so it is constructed inline and never stored, never
    // named in the header, and never allowed out of this function.
    //
    // RootDir is where JSBSim resolves anything relative; the other three are
    // the folders it searches for the three kinds of file an aircraft is built
    // from. The systems path is set for completeness - the stock F-16 keeps its
    // own systems inside its aircraft folder, which JSBSim finds by itself.
    //
    // The three subfolders are named RELATIVELY, and must be: JSBSim joins a
    // relative path onto RootDir itself. Giving it the full path here produces
    // "assets/jsbsim/assets/jsbsim/aircraft/..." and a file-not-found that
    // reads as if the data were missing rather than doubled.
    im.fdm.SetRootDir(SGPath(dataDir));
    im.fdm.SetAircraftPath(SGPath("aircraft"));
    im.fdm.SetEnginePath(SGPath("engine"));
    im.fdm.SetSystemsPath(SGPath("systems"));

    // JSBSim reports a bad or missing aircraft description by THROWING, and an
    // uncaught exception here would take the editor down over a mistyped name
    // in the Inspector. Catching it turns that into a message in a text box,
    // which is the same bargain the model loader makes with a missing .glb.
    try {
        // `false` means "do not add the model name to the aircraft path": the
        // path set above already points at the folder that contains it.
        if (!im.fdm.LoadModel(aircraft, true)) {
            im.error = "could not load aircraft '" + aircraft + "' from " + dataDir;
            return false;
        }
    } catch (const std::exception& e) {
        im.error = "JSBSim failed to load '" + aircraft + "': " + e.what();
        return false;
    } catch (...) {
        im.error = "JSBSim failed to load '" + aircraft + "' (unknown error)";
        return false;
    }

    im.ready = true;
    return true;
}

// --- Starting a run -------------------------------------------------------------

void FlightModel::Reset(const FlightStart& start) {
    Impl& im = *m_impl;
    if (!im.ready) return;

    // Remember where in the game world this run begins. Everything JSBSim
    // reports afterwards is an offset from the latitude and longitude below,
    // and this is what turns that offset back into a scene coordinate.
    im.originX = start.x;
    im.originZ = start.z;
    im.accumulator = 0.0f;
    im.lastSteps = 0;

    // Describe the flight condition. Writing to `ic/...` does not move the
    // aircraft - it fills in the condition that RunIC() will then build.
    im.Set(kIcLatDeg, kWorldLatDeg);
    im.Set(kIcLonDeg, kWorldLonDeg);
    im.Set(kIcAltFt, start.y / kFeetToMetres);
    im.Set(kIcVcKts, start.airspeedKt);
    im.Set(kIcHeadingDeg, start.headingDeg);
    im.Set(kIcGammaDeg, start.pathAngleDeg);   // climb angle, 0 = level
    im.Set(kIcPhiDeg, 0.0);                    // wings level

    // Start with the engine already running and spooled. -1 means "every
    // engine". Without this the F-16 loads cold: the throttle does nothing at
    // all and the aircraft simply glides, with no error to explain it.
    im.Set(kSetRunning, -1.0);

    // Controls to a sane hands-off position before the condition is built, so
    // that a restart does not inherit whatever the last run's stick was doing.
    im.controls = FlightControls{};
    im.controls.throttle = 0.8f;   // enough thrust to hold the entry speed
    im.PushControls();

    im.fdm.RunIC();   // build the state described above

    if (start.trim) {
        // Solve for the elevator and angle of attack that hold this condition.
        // Without it the aircraft begins with whatever pitch moment its trimmed-
        // out surfaces happen to produce, and the first second of every run is
        // a bobble that looks like a bug in the controls.
        //
        // A trim can legitimately FAIL - asked for a condition the aircraft
        // cannot hold, such as level flight at 60 knots - and JSBSim throws
        // when it does. That is not fatal: an untrimmed start still flies, it
        // just starts out of balance, so the failure is swallowed here rather
        // than refusing to begin the run.
        try {
            im.fdm.DoTrim(kTrimLongitudinal);
        } catch (...) {
            // Deliberately ignored; see above.
        }
    }

    im.Sample();
}

// --- Driving --------------------------------------------------------------------

void FlightModel::SetControls(const FlightControls& controls) {
    m_impl->controls = controls;
}

void FlightModel::Advance(float dt) {
    Impl& im = *m_impl;
    im.lastSteps = 0;
    if (!im.ready) return;

    // JSBSim carries its own step size, set by the aircraft description (120 Hz
    // for the stock F-16). Reading it rather than hardcoding it means an
    // aircraft that asks for a different rate simply gets it.
    const float step = static_cast<float>(im.fdm.GetDeltaT());
    if (step <= 0.0f) return;   // a suspended simulation; nothing to spend time on

    // The controls are written once per frame rather than once per step. They
    // do not change within a frame - the keyboard is read once - and pushing
    // eight identical values through the property tree would be pure waste.
    im.PushControls();

    // Bank this frame's real time, then spend it in whole steps. The leftover
    // stays in the accumulator and is carried into the next frame, so no time
    // is ever lost or counted twice. This is the same arrangement UpdatePhysics
    // uses for the rigid-body world, for the same reason: an integrator is only
    // stable when its step size is constant.
    im.accumulator += dt;
    while (im.accumulator >= step && im.lastSteps < kMaxStepsPerFrame) {
        im.accumulator -= step;
        ++im.lastSteps;
        im.fdm.Run();
    }

    // If the loop hit its ceiling there is still a backlog banked, and keeping
    // it would guarantee another full frame of catching up - the spiral the
    // ceiling exists to break. Throw it away: the aircraft runs momentarily
    // slow, which is invisible, rather than falling further behind every frame.
    if (im.lastSteps == kMaxStepsPerFrame) im.accumulator = 0.0f;

    // Only re-read the state if the simulation actually moved. A frame short
    // enough to run no steps leaves the previous sample in place, which is
    // correct: nothing changed.
    if (im.lastSteps > 0) im.Sample();
}

} // namespace eng
