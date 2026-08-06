#include "engine/components/JSBSim.h"

#include "engine/Scene.h"
#include "engine/components/Terrain.h"   // GroundHeightAt, and finding the terrain
#include "imgui.h"
#include "raymath.h"

#include <cmath>

namespace eng {

namespace {

// A zeroed state, handed out when there is no simulation. Returning a reference
// to this rather than nothing at all is what lets a caller read State() without
// checking Ready() first: an aircraft that is not flying reads as stationary,
// which is true.
const FlightState& NoState() {
    static const FlightState empty;
    return empty;
}

// The compass heading an orientation is facing, degrees, 0 = north.
//
// The engine's world is mapped so that -Z is North and +X is East (see
// FlightModel.h), and an unrotated object faces its own -Z. So rotating -Z by
// the orientation gives the direction of the nose in world space, and the
// heading is the angle of that direction round the compass.
//
// atan2 takes the EAST component first and the NORTH component second, which is
// the reverse of the usual atan2(y, x) habit - because compass bearings are
// measured clockwise from north, not anticlockwise from east.
float HeadingFromRotation(Quaternion rotation) {
    const Vector3 forward = Vector3RotateByQuaternion({0.0f, 0.0f, -1.0f}, rotation);

    // Straight up or straight down: the nose has no compass direction at all,
    // so any answer is as good as another. Zero rather than whatever atan2 does
    // with two near-zero arguments.
    const float east = forward.x, north = -forward.z;
    if (std::fabs(east) < 1e-6f && std::fabs(north) < 1e-6f) return 0.0f;

    float deg = std::atan2(east, north) * RAD2DEG;
    if (deg < 0.0f) deg += 360.0f;    // atan2 returns -180..180; headings are 0..360
    return deg;
}

} // anonymous namespace

const FlightState& JSBSimComponent::State() const {
    return m_fdm ? m_fdm->State() : NoState();
}

// ---- Lifecycle ---------------------------------------------------------------

void JSBSimComponent::OnStart(Entity& owner) {
    m_error.clear();
    m_fdm.reset();          // a restart must not inherit the last run's aircraft
    m_controls = FlightControls{};
    m_onGround = false;

    // Unticked means "this component is not the thing flying the aircraft", so
    // nothing is built at all. See the warning in the header: this is how the
    // JSBSim model and flight_sim.lua are switched between.
    if (!enabled) return;

    auto fdm = std::make_unique<FlightModel>();
    if (!fdm->Load(dataDir, aircraft)) {
        // A missing or malformed aircraft description is a runtime fact - the
        // data is read from disk - so it is recorded and shown rather than
        // being allowed to crash the editor or, worse, to do nothing quietly.
        // The entity simply does not fly, exactly as a missing model file draws
        // a fallback instead of taking the frame down.
        m_error = fdm->Error();
        return;
    }

    // WHERE the run starts comes from the entity's authored transform, not from
    // fields on this component: the aircraft is placed in the editor like
    // anything else, and dragging it somewhere new is expected to move where it
    // takes off from.
    //
    // The WORLD transform, not the local one, because an entity under a parent
    // is drawn at its world position and that is where it must begin flying.
    Vector3    pos = owner.transform.position;
    Quaternion rot = owner.transform.rotation;
    if (Scene* scene = Scene::Current(); scene && owner.parent != kInvalidEntity) {
        const Matrix world = scene->WorldMatrix(owner, /*ignoreScale=*/true);
        Vector3 wscale;
        MatrixDecompose(world, &pos, &rot, &wscale);
    }

    FlightStart start;
    start.x = pos.x;
    start.y = pos.y;
    start.z = pos.z;
    // Only the HEADING is taken from the authored rotation. JSBSim begins
    // wings-level on the flight path angle given below, so an aircraft authored
    // banked or nose-high starts pointing the same way round the compass but
    // flying straight - which is what a sane starting condition is. A pitch
    // authored into the model's rest pose would otherwise become a dive.
    start.headingDeg   = HeadingFromRotation(rot);
    start.airspeedKt   = startAirspeedKt;
    start.pathAngleDeg = startPathAngleDeg;
    start.trim         = trimOnStart;
    start.fuelLb       = startFuelLb;

    // Where the ground is under the starting point, so the simulation agrees
    // with the landscape from its very first step. An aircraft authored at
    // 1000 m over a 700 m ridge has 300 m beneath it, not 1000, and the trim is
    // solved against that.
    if (Scene* scene = Scene::Current())
        start.terrainElevationM = GroundHeightAt(*scene, start.x, start.z);

    fdm->Reset(start);

    // Adopt the controls the aircraft was TRIMMED at, rather than leaving them
    // at zero. Trimming solves for the throttle and elevator that hold the
    // starting condition, and starting from zero would throw that answer away
    // the moment the first frame pushed the controls back in - the aircraft
    // would settle into a stable cruise and then, with no error anywhere, close
    // the throttle and glide.
    //
    // A script that sets the controls overwrites these on its first update, so
    // this costs nothing where there is one and is the difference between
    // flying and gliding where there is not.
    m_controls = fdm->Controls();

    m_fdm = std::move(fdm);

    // Put the entity where the trimmed aircraft actually ended up. Trimming can
    // move it slightly, and more importantly it settles the ATTITUDE onto the
    // angle of attack that holds the condition - so without this the first
    // drawn frame shows the authored pose and the second snaps to the trimmed
    // one, which reads as a glitch on every Play.
    OnUpdate(0.0f, owner);
}

// Report the aircraft touching the ground, through the SAME hook a physics
// impact uses. A script therefore handles hitting a hill with the onCollision
// it already has, rather than with a second mechanism that only aircraft have.
//
// WHY THIS IS HERE AT ALL, rather than in the physics world. The rigid-body
// simulation reports a contact only when at least one of the two bodies is
// DYNAMIC. An aircraft flown by a flight model is Kinematic - the flight model
// owns where it is, and a dynamic body would fight it for that - and the
// landscape is Static. Kinematic against Static is a pair the simulation never
// tests, so it produces no contact, no matter how squarely the two meet.
// Measured: a kinematic capsule dropped from 2000 m through a 440 m hill
// reported nothing at any point. So the ground contact has to come from the
// side that actually knows about the ground, which is the flight model.
//
// Fires ONCE per touchdown, on the transition from airborne to touching, which
// is what a contact event means everywhere else in the engine - an aircraft
// sitting on a runway should not be reporting a collision sixty times a second.
void JSBSimComponent::ReportGroundContact(Entity& owner, const FlightState& s) {
    // TWO thresholds, not one, and the gap between them is the point.
    //
    // Height above ground is measured to the aircraft's reference point, so
    // kTouchM is a small margin standing in for the distance down to whatever
    // touches first - wheels if they are down, the belly if they are not.
    //
    // kClearM is the height it must regain before it counts as airborne again,
    // and it is deliberately much higher. With a single threshold the aircraft
    // sits right on it: the ground reactions push it up a few centimetres, it
    // settles back, and every one of those flutters reads as a fresh impact.
    // Measured before this gap existed: one descent into a hillside reported
    // two strikes. Requiring it to properly get away before it can arrive again
    // is what a contact event means - the moment two things BEGIN touching -
    // and it still reports a genuine bounce, where an aircraft skips off the
    // ground and comes back down.
    constexpr float kTouchM = 0.5f;
    constexpr float kClearM = 5.0f;

    if (m_onGround) {
        // Already down. Only clear the flag once it is convincingly flying.
        if (s.altitudeAglM > kClearM) m_onGround = false;
        return;
    }
    if (s.altitudeAglM > kTouchM) return;   // still airborne; nothing to report
    m_onGround = true;

    Scene* scene = Scene::Current();
    if (!scene) return;

    // The thing struck is the terrain entity, so a script can read its tag and
    // treat hitting the ground differently from hitting an aircraft. If the
    // scene has no terrain there is nothing to name as the other party, and the
    // event is skipped rather than invented.
    Entity* ground = nullptr;
    for (Entity& e : scene->Entities())
        if (e.GetComponent<TerrainComponent>()) { ground = &e; break; }
    if (!ground) return;

    // How hard. The DOWNWARD speed rather than the total, because that is what
    // separates a landing from a crash: an aircraft crossing a valley at 600
    // m/s is not hitting anything, and one settling onto a runway at 2 m/s has
    // touched the same ground as one arriving at 200.
    const float closingSpeed = -s.vy > 0.0f ? -s.vy : 0.0f;
    const Vector3 point{s.x, s.y, s.z};

    // Every component on the aircraft, which is how the physics contact
    // dispatcher does it - the script is the usual listener, but nothing here
    // assumes that is the only one.
    //
    // The pointers are SNAPSHOT before any hook runs. A script that adds a
    // component while being notified would otherwise reallocate the vector
    // being walked and leave this loop holding freed memory.
    std::vector<Component*> comps;
    comps.reserve(owner.components.size());
    for (const auto& c : owner.components) comps.push_back(c.get());
    for (Component* c : comps) c->OnCollision(owner, *ground, closingSpeed, point);
}

void JSBSimComponent::OnDestroy(Entity& owner) {
    // Release the simulation as soon as the entity goes. It is runtime state
    // and nothing outside the run should be holding a whole aerodynamic model.
    m_fdm.reset();
}

void JSBSimComponent::OnUpdate(float dt, Entity& owner) {
    if (!m_fdm || !m_fdm->Ready()) return;

    Scene* scene = Scene::Current();

    // Tell the simulation how high the ground is HERE, before it takes a step.
    //
    // A flight model has a ground in it - undercarriage, wheels that take
    // weight, an airframe that strikes - but no idea what shape the landscape
    // is. Left untold it believes the world is a flat sea at zero, so an
    // aircraft at 300 m over a 700 m mountain reads as 300 m up in clear air
    // and flies straight through the hill. This one line is what makes the
    // landscape exist as far as the aerodynamics are concerned.
    //
    // Sampled at the aircraft's own position and therefore re-sampled every
    // frame, because the answer changes as it moves.
    if (scene) {
        const FlightState& prev = m_fdm->State();
        m_fdm->SetTerrainElevation(GroundHeightAt(*scene, prev.x, prev.z));
    }

    // Pushed every frame rather than once at the start, so the box can be
    // ticked or unticked while the aircraft is flying and take effect at once.
    m_fdm->SetUnlimitedFuel(unlimitedFuel);

    // Controls next, then time. The inputs a script set this frame must be in
    // place before the steps that respond to them run.
    m_fdm->SetControls(m_controls);
    m_fdm->Advance(dt);

    const FlightState& s = m_fdm->State();

    // Hand the entity the velocity the SIMULATION integrated, replacing the
    // estimate Scene::Update measured from two positions.
    //
    // The estimate is not wrong so much as unavoidably coarse here. The flight
    // model advances in whole steps of its own fixed rate - 120 Hz - so in a
    // variable-length frame it moves a whole number of those steps, not exactly
    // one frame's worth. Dividing that quantised distance by the frame's
    // duration is off by however much the two disagree: measured, a 19.5 ms
    // frame that fitted two 8.33 ms steps read 14.5% slow. The state below has
    // no such error, because it was never a difference of two samples.
    owner.velocity = {s.vx, s.vy, s.vz};

    ReportGroundContact(owner, s);

    // WHERE THE AIRCRAFT IS *NOW*, not where the last completed step left it.
    //
    // The flight model only advances in whole steps of its own fixed rate, so
    // after a frame there is almost always a leftover of time it has been given
    // but not yet simulated. Its state therefore describes a moment slightly in
    // the past, and drawing the aircraft there makes it move in LUMPS: measured,
    // a 20 ms frame and a 14 ms frame both moved it exactly 5.28 m, because both
    // spent the same two steps.
    //
    // At a steady 60 fps this is invisible - a frame is exactly two of the
    // F-16's 120 Hz steps and there is never a leftover. It only shows when the
    // frame rate moves, and then it shows twice over: the aircraft judders, and
    // anything a script positions by predicting where the aircraft will be
    // (a muzzle flash, the engine plume) lands up to a step's travel away from
    // it - nearly two metres at cruise, changing every frame, which reads as the
    // effect glitching back and forth.
    //
    // Carrying the state forward by the unspent time along the velocity puts the
    // aircraft where it actually is at this instant. That is the standard cure
    // for watching a fixed-rate simulation through variable-length frames, and
    // it makes the motion smooth AND the predictions exact at the same time.
    //
    // Only the POSITION is carried forward. Doing the same for the orientation
    // would need the angular rate and would buy far less: a tenth of a degree of
    // rotation is invisible where two metres of position is not.
    const float pending = m_fdm->PendingSeconds();
    const Vector3    worldPos{s.x + s.vx * pending,
                              s.y + s.vy * pending,
                              s.z + s.vz * pending};
    const Quaternion worldRot{s.qx, s.qy, s.qz, s.qw};

    // The simulation works in WORLD space; an entity stores a LOCAL transform
    // relative to its parent. With no parent - which is what an aircraft
    // normally is - the two are the same thing and the values go straight
    // across. This is the same world/local bridge Physics.cpp crosses when it
    // writes a simulated body back onto its entity.
    const Entity* parent =
        (scene && owner.parent != kInvalidEntity) ? scene->FindConst(owner.parent)
                                                  : nullptr;
    if (!parent) {
        owner.transform.position = worldPos;
        owner.transform.rotation = worldRot;
        return;
    }

    // world = local * parentWorld, so local = world * inverse(parentWorld).
    const Matrix parentWorld = scene->WorldMatrix(*parent, /*ignoreScale=*/true);
    const Matrix world = MatrixMultiply(QuaternionToMatrix(worldRot),
                                        MatrixTranslate(worldPos.x, worldPos.y,
                                                        worldPos.z));
    const Matrix local = MatrixMultiply(world, MatrixInvert(parentWorld));

    Vector3    lpos, lscale;
    Quaternion lrot;
    MatrixDecompose(local, &lpos, &lrot, &lscale);
    owner.transform.position = lpos;
    owner.transform.rotation = lrot;
    // Scale is deliberately left alone. The flight model was never told about
    // it and has no opinion, so overwriting the authored scale with a decomposed
    // approximation would make the aircraft creep in size over a long run.
}

// ---- Inspector ----------------------------------------------------------------

void JSBSimComponent::OnInspector() {
    ImGui::Checkbox("Enabled", &enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Untick to leave this entity to something else.\n"
                          "ONLY ONE thing may move an aircraft: if a flight\n"
                          "script and this component both write the transform\n"
                          "they fight, and the result is jitter rather than a\n"
                          "blend. This is the switch between the two models.");

    // ImGui edits a fixed-size char buffer, not a std::string, so the value is
    // copied out, edited, and copied back only if it actually changed. The
    // buffers are generous: these are folder and file names, not prose.
    char aircraftBuf[64];
    std::snprintf(aircraftBuf, sizeof(aircraftBuf), "%s", aircraft.c_str());
    if (ImGui::InputText("Aircraft", aircraftBuf, sizeof(aircraftBuf)))
        aircraft = aircraftBuf;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A subfolder of <data>/aircraft holding a matching\n"
                          ".xml - \"f16\" is the one shipped with this project.\n"
                          "Read from disk on Play, so a wrong name shows up as\n"
                          "an error below rather than as a build failure.");

    char dirBuf[260];
    std::snprintf(dirBuf, sizeof(dirBuf), "%s", dataDir.c_str());
    if (ImGui::InputText("Data folder", dirBuf, sizeof(dirBuf)))
        dataDir = dirBuf;

    ImGui::DragFloat("Start speed (kt)", &startAirspeedKt, 1.0f, 0.0f, 1200.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Indicated airspeed the run begins at. Too low and\n"
                          "the aircraft starts already stalled - there is no\n"
                          "such thing as placing one at rest in mid-air.");

    ImGui::DragFloat("Start climb (deg)", &startPathAngleDeg, 0.5f, -60.0f, 60.0f);

    ImGui::Checkbox("Trim on start", &trimOnStart);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Solve for the controls that hold the starting\n"
                          "condition, so the run does not open with the\n"
                          "aircraft settling. Leave on unless investigating\n"
                          "what the untrimmed aircraft does.");

    // Fuel load. Zero is a real setting here, not an empty field, so the label
    // has to say what it means or it reads as "no fuel".
    ImGui::DragFloat("Start fuel (lb)", &startFuelLb, 25.0f, 0.0f, 20000.0f,
                     startFuelLb <= 0.0f ? "as the aircraft says"
                                         : "%.0f lb");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How much fuel to take off with. ZERO means whatever\n"
                          "the aircraft description carries - 3000 lb for the\n"
                          "stock F-16.\n\n"
                          "Spread across the tanks in the proportions the\n"
                          "aircraft was authored with, so the centre of gravity\n"
                          "stays put and tanks it carries EMPTY stay empty.\n"
                          "Capacity still applies: the F-16's two internal tanks\n"
                          "hold about 6970 lb between them, and more than that\n"
                          "is clamped.\n\n"
                          "Does nothing visible while Unlimited fuel is on.");

    ImGui::Checkbox("Unlimited fuel", &unlimitedFuel);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Keep the tanks topped up. ON by default because the\n"
                          "stock F-16 starts 43%% full, which at combat power is\n"
                          "under FOUR MINUTES - and running dry looks exactly\n"
                          "like a bug: the throttle stops doing anything and the\n"
                          "aircraft will not accelerate even pointed at the\n"
                          "ground. Untick for fuel as a real constraint.");

    ImGui::TextDisabled("Position and heading come from the transform.");

    // --- The live readout ----------------------------------------------------
    // Worth the space: these are the numbers that say whether the aircraft is
    // flying properly, and every one of them is invisible from outside. An
    // aircraft mushing along at 25 degrees of alpha looks, from the viewport,
    // exactly like one flying normally.
    if (!m_error.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Load failed");
        ImGui::TextWrapped("%s", m_error.c_str());
        return;
    }

    if (!Ready()) return;

    const FlightState& s = m_fdm->State();
    ImGui::Separator();
    ImGui::Text("%.0f kt  M%.2f", s.airspeedKt, s.mach);
    ImGui::Text("%.0f m  (%.0f ft)", s.altitudeM, s.altitudeFt);
    ImGui::Text("%.0f m above ground%s", s.altitudeAglM,
                m_onGround ? "  [ON GROUND]" : "");
    ImGui::Text("alpha %.1f  beta %.1f  %.1f g", s.alphaDeg, s.betaDeg,
                s.loadFactor);
    ImGui::Text("roll %.0f  pitch %.0f  hdg %.0f", s.rollDeg, s.pitchDeg,
                s.headingDeg);
    ImGui::Text("engine %.0f%%", s.enginePower * 100.0f);

    // Fuel is called out in red once it is nearly gone, because an engine that
    // has quit for want of fuel is otherwise indistinguishable from one that
    // has quit for a reason worth debugging.
    if (!unlimitedFuel && s.fuelFraction < 0.1f)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f),
                           "fuel %.0f lb (%.0f%%) - NEARLY DRY",
                           s.fuelLb, s.fuelFraction * 100.0f);
    else
        ImGui::Text("fuel %.0f lb (%.0f%%)", s.fuelLb, s.fuelFraction * 100.0f);
    ImGui::TextDisabled("%d steps last frame", m_fdm->LastStepCount());
}

} // namespace eng
