#include "engine/components/JSBSim.h"

#include "engine/Scene.h"
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

    fdm->Reset(start);
    m_fdm = std::move(fdm);

    // Put the entity where the trimmed aircraft actually ended up. Trimming can
    // move it slightly, and more importantly it settles the ATTITUDE onto the
    // angle of attack that holds the condition - so without this the first
    // drawn frame shows the authored pose and the second snaps to the trimmed
    // one, which reads as a glitch on every Play.
    OnUpdate(0.0f, owner);
}

void JSBSimComponent::OnDestroy(Entity& owner) {
    // Release the simulation as soon as the entity goes. It is runtime state
    // and nothing outside the run should be holding a whole aerodynamic model.
    m_fdm.reset();
}

void JSBSimComponent::OnUpdate(float dt, Entity& owner) {
    if (!m_fdm || !m_fdm->Ready()) return;

    // Controls first, then time. The inputs a script set this frame must be in
    // place before the steps that respond to them run.
    m_fdm->SetControls(m_controls);
    m_fdm->Advance(dt);

    const FlightState& s = m_fdm->State();

    const Vector3    worldPos{s.x, s.y, s.z};
    const Quaternion worldRot{s.qx, s.qy, s.qz, s.qw};

    // The simulation works in WORLD space; an entity stores a LOCAL transform
    // relative to its parent. With no parent - which is what an aircraft
    // normally is - the two are the same thing and the values go straight
    // across. This is the same world/local bridge Physics.cpp crosses when it
    // writes a simulated body back onto its entity.
    Scene* scene = Scene::Current();
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
    ImGui::Text("alpha %.1f  beta %.1f  %.1f g", s.alphaDeg, s.betaDeg,
                s.loadFactor);
    ImGui::Text("roll %.0f  pitch %.0f  hdg %.0f", s.rollDeg, s.pitchDeg,
                s.headingDeg);
    ImGui::Text("engine %.0f%%", s.enginePower * 100.0f);
    ImGui::TextDisabled("%d steps last frame", m_fdm->LastStepCount());
}

} // namespace eng
