#pragma once

#include "engine/Component.h"   // the Component base class we derive from

#include "raylib.h"        // Color, Camera3D, Matrix, drawing types
#include "sol/sol.hpp"     // sol2: a C++ wrapper that runs Lua and binds C++ to it

#include <optional>        // std::optional, for an omitted colour name
#include <string>
#include <utility>         // std::pair
#include <vector>

namespace eng {

// ============================================================================
// ScriptComponent: makes an entity run a Lua script file every frame.
// ----------------------------------------------------------------------------
// Each ScriptComponent owns its OWN Lua interpreter ("state"). That isolation
// means one script's variables can't clash with another's, and reloading one
// script never disturbs the others.
// ============================================================================
class ScriptComponent : public Component {   // ": public Component" = inherits it
public:
    // The label shown on this component's header in the Inspector.
    const char* Name() const override { return "Script"; }

    // Allow several scripts on one entity (for example one for movement and
    // one for shooting). `override` tells the compiler this replaces a virtual
    // function from the base class, and errors out if the signature is wrong.
    bool AllowMultiple() const override { return true; }

    // Copying carries over the file PATH and the tuned property values, but
    // not the live Lua state (an interpreter mid-run is runtime-only, and a
    // fresh copy loads its own when play begins).
    //
    // The property values MUST be copied. Pressing Play deep-copies the scene
    // and keeps that copy as the backup to restore on Stop, so anything a
    // Clone leaves behind is not merely missing from the copy - it is erased
    // from the project the moment playing stops.
    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<ScriptComponent>();
        c->path    = path;
        c->m_props = m_props;
        return c;
    }

    // Lifecycle hooks (bodies are in the .cpp). OnStart loads the file and
    // calls the script's onStart; OnUpdate calls its onUpdate each frame;
    // OnDestroy calls its onDestroy.
    void OnStart(Entity& owner) override;
    void OnDestroy(Entity& owner) override;
    void OnUpdate(float dt, Entity& owner) override;
    void OnCollision(Entity& owner, Entity& other, float speed,
                     Vector3 point) override;
    // Calls the script's optional onDrawHud(entity, w, h), where it may use
    // the `draw.*` API to put things on screen over the finished 3D view. This
    // is what lets a HUD element be authored in a script - and, because
    // GraphComponent derives from this class, in a node graph - instead of
    // needing C++ drawing code written for it.
    void OnDrawHud(const Entity& owner, int width, int height) override;

    // Save the script path, plus ONLY the properties this entity has actually
    // overridden in the Inspector.
    //
    // Saving all of them would be a trap: the moment a scene was saved, every
    // value in the script's `properties` table would be frozen into the scene
    // file, and from then on editing the .lua would appear to do nothing -
    // the stored copy always wins. Writing only real overrides means an
    // untouched property keeps following whatever the script says.
    void Serialize(nlohmann::json& out) const override {
        out["path"] = path;
        nlohmann::json props = nlohmann::json::object();
        for (const auto& pr : m_props)
            if (pr.overridden) props[pr.name] = pr.value;
        out["props"] = props;
    }
    void Deserialize(const nlohmann::json& in) override {
        path = in.value("path", path);
        m_props.clear();
        // Anything stored in the file was written because it differed from the
        // script, so it comes back as an override.
        if (in.contains("props") && in["props"].is_object())
            for (auto it = in["props"].begin(); it != in["props"].end(); ++it)
                m_props.push_back({it.key(), it.value().get<float>(), true});
    }

    // Draws the path field + a Load/Reload button + any error text.
    void OnInspector() override;

    // (Re)read the .lua file into a fresh Lua state. Errors do NOT crash the
    // program: they are captured into m_error and shown in the Inspector.
    void Load();

    // The script file to run. Public so the editor and spawn code can set it.
    std::string path = "assets/scripts/spin.lua";

    // The last load or compile error, or "" if the script is fine. Read-only to
    // callers; the Inspector shows it, and tools can report it.
    const char* ErrorText() const { return m_error.c_str(); }
    bool        IsLoaded()  const { return m_loaded; }

    // Run this Lua SOURCE instead of reading `path`, when it is not empty.
    //
    // This is how a node graph runs without a .lua file existing: something
    // compiles the graph into source, hands it here, and the script loads from
    // memory. It is deliberately NOT serialized - generated code is not
    // something to store in a scene file, and leaving it out of Serialize is
    // what makes "compiled fresh on Play, never written to disk" the natural
    // behaviour rather than a special case.
    std::string source;

protected:
    // (Re)read the script's `properties` table into m_props and draw them as
    // Inspector fields. Split out so that a component whose source comes from
    // somewhere else - a graph, say - presents its tunables identically,
    // including the override markers and revert buttons.
    void DrawPropertiesInspector();

    sol::state m_lua;                       // this script's private Lua interpreter
    // Handles to the script's optional functions. A sol::protected_function
    // can be called safely: if the Lua code errors, we get an error result
    // instead of a crash.
    sol::protected_function m_onStart;
    sol::protected_function m_onUpdate;
    sol::protected_function m_onDestroy;
    sol::protected_function m_onCollision;
    sol::protected_function m_onDrawHud;
    bool        m_loaded = false;           // did the file load without error?
    std::string m_error;                    // last error message, "" if none

    // One tunable value the script exposed via its global `properties` table.
    struct ScriptProp {
        std::string name;
        float       value = 0.0f;
        // Has this entity been given its own value for this property, replacing
        // the script's? Only overrides are saved to the scene, and only
        // overrides survive a reload - everything else re-reads the script, so
        // editing a default in the .lua takes effect the next time it loads.
        bool        overridden = false;
    };

    // The script's tunables, sorted by name. Shown as editable fields in the
    // Inspector.
    std::vector<ScriptProp> m_props;
};

// ============================================================================
// GraphComponent: an entity whose behaviour is a NODE GRAPH rather than a
// hand-written script.
// ----------------------------------------------------------------------------
// It derives from ScriptComponent because that is exactly what it is: a script
// whose source happens to be generated. Everything about running Lua - the
// private interpreter, the lifecycle hooks, the `properties` table and its
// Inspector fields with their override markers - is inherited unchanged. The
// only thing this adds is where the source comes from.
//
// The graph is compiled to Lua IN MEMORY when play begins. No .lua file is
// written, ever: a generated file is a copy that can drift from the graph that
// produced it, and there is no reason to keep one when the graph is right there.
//
// Compiling needs the node editor's code generator, which lives in the editor,
// while this component lives in the engine - and the engine must not depend on
// the editor. So the engine declares the seam (SetGraphCompiler below) and
// whoever hosts the engine fills it in.
// ============================================================================

// Turns a graph file into Lua source. Returns false and fills `outError` if the
// graph cannot be read.
using GraphCompiler = bool (*)(const std::string& graphPath,
                               std::string& outLua, std::string& outError);

// Register the function used to compile graphs. Called once by the host (the
// editor does it at startup). With none registered, a GraphComponent reports
// that plainly instead of failing in some obscure way.
void SetGraphCompiler(GraphCompiler fn);
bool HasGraphCompiler();

class GraphComponent : public ScriptComponent {
public:
    const char* Name() const override { return "Graph"; }
    bool AllowMultiple() const override { return true; }

    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<GraphComponent>();
        c->graphPath = graphPath;
        c->m_props   = m_props;   // tuned values survive Play/Stop, as for scripts
        return c;
    }

    void OnStart(Entity& owner) override;
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["graph"] = graphPath;
        nlohmann::json props = nlohmann::json::object();
        for (const auto& pr : m_props)
            if (pr.overridden) props[pr.name] = pr.value;
        out["props"] = props;
    }
    void Deserialize(const nlohmann::json& in) override {
        graphPath = in.value("graph", graphPath);
        m_props.clear();
        if (in.contains("props") && in["props"].is_object())
            for (auto it = in["props"].begin(); it != in["props"].end(); ++it)
                m_props.push_back({it.key(), it.value().get<float>(), true});
    }

    // Compile the graph and load the result. Returns false on failure, with the
    // reason in the inherited error field, which the Inspector already shows.
    bool Recompile();

    // The graph file this entity runs, e.g. "assets/graphs/enemy_graph.json".
    std::string graphPath;

    // Set by the Inspector's buttons and cleared by the editor once it has
    // acted. Opening a graph in the node editor, and creating a new one, both
    // need the editor - so rather than the engine reaching into it, these just
    // record that the user asked. Runtime only; never serialized.
    bool editRequested = false;
    bool newRequested  = false;

    GraphComponent() { path.clear(); }   // a graph has no .lua path of its own

protected:
    // Fill `source` by compiling the graph. Reports why in m_error if it cannot.
    bool CompileSource();
};

// ============================================================================
// ShapeComponent: draws the entity as a simple colored 3D primitive.
// An entity with no ShapeComponent (and no other visual) is invisible but
// still exists in the world.
// ============================================================================
class ShapeComponent : public Component {
public:
    // The primitive shapes we can draw. `enum class` is a strongly-typed
    // enumeration. The explicit "= 0" fixes Cube's number; the rest follow
    // (Sphere=1, ...). Those numbers are written into save files, so the
    // order must stay stable or old scenes would load the wrong shape.
    enum class Kind { Cube = 0, Sphere, Cylinder, Cone, Plane };

    const char* Name() const override { return "Shape"; }

    // This component is plain data (an enum, a color, a bool), so the
    // compiler-generated copy constructor (*this) copies it correctly.
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<ShapeComponent>(*this);
    }

    void OnDraw(const Entity& owner) override;      // draw the primitive
    void OnInspector() override;                    // shape/color editing UI

    void Serialize(nlohmann::json& out) const override {
        out["kind"]      = (int)kind;               // store the enum as its number
        out["tint"]      = {tint.r, tint.g, tint.b, tint.a};   // color as RGBA
        out["wireframe"] = wireframe;
    }
    void Deserialize(const nlohmann::json& in) override {
        kind = (Kind)in.value("kind", 0);           // missing -> Cube (0)
        if (in.contains("tint"))
            tint = {in["tint"][0], in["tint"][1], in["tint"][2], in["tint"][3]};
        wireframe = in.value("wireframe", wireframe);
    }

    Kind  kind = Kind::Cube;    // which primitive to draw
    Color tint = MAROON;        // its color (MAROON is a raylib preset)
    bool  wireframe = true;     // also draw black edge lines around it?
};

// ============================================================================
// CameraComponent: makes its entity act as a camera. The Game view renders
// the world through it. Because the camera's position and orientation come
// from the entity's transform, anything that moves the entity (a script, a
// parent) also moves the view.
// ============================================================================
class CameraComponent : public Component {
public:
    const char* Name() const override { return "Camera"; }
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<CameraComponent>(*this);
    }
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["fovy"] = fovy;
        out["nearClip"] = nearClip;  out["farClip"] = farClip;
    }
    void Deserialize(const nlohmann::json& in) override {
        fovy     = in.value("fovy", fovy);
        // Scenes saved before clipping planes existed carry neither key, so they
        // fall back to the defaults below and keep rendering as they always did.
        nearClip = in.value("nearClip", nearClip);
        farClip  = in.value("farClip",  farClip);
    }

    // Turn an entity's world matrix (from Scene::WorldMatrix) into the
    // Camera3D struct that raylib needs to render a 3D view. Doing it from the
    // world matrix is what lets a camera parented to the jet follow it.
    Camera3D ToCamera3D(const Matrix& world) const;

    float fovy = 60.0f;         // vertical field of view in degrees (zoom)

    // ---- Clipping planes: the near and far edges of what this camera can see.
    //
    // A perspective camera does not see an infinite space. It sees a truncated
    // pyramid - a "frustum" - and these two distances are its front and back
    // faces. Anything nearer than `nearClip` or further than `farClip` is
    // discarded before it is ever drawn. `farClip` is therefore the VIEW
    // DISTANCE: with terrain tens of thousands of units across, a far plane left
    // at a few thousand means most of the landscape simply never appears, and it
    // looks like the world ends in mid-air.
    //
    // The obvious move is to push the far plane out as far as possible, but that
    // is not free, and the reason is the DEPTH BUFFER. To decide which surface is
    // in front, the GPU stores a depth per pixel in a fixed number of bits. That
    // value is not spread evenly through the frustum: it is bunched up close to
    // the camera and stretched thin far away. How badly it is stretched depends
    // almost entirely on the RATIO far/near - not on either number alone. Once
    // two distant surfaces fall within the same depth step, the GPU can no longer
    // tell which is in front, and they flicker against each other as the camera
    // moves. That flicker is called Z-FIGHTING.
    //
    // The lever that fixes it is the NEAR plane, not the far one. Doubling the
    // far distance costs the same precision as halving the near distance buys
    // back, so a camera that never gets within a metre of anything - a chase
    // camera behind an aircraft, say - should push its near plane out and spend
    // the ratio on distance instead. That is exactly the trade the defaults make:
    // 0.3 to 25000 is the same ratio as the graphics library's own 0.05 to 4000
    // default, so the precision is no worse than before while seeing six times
    // further.
    //
    // Raise `nearClip` further (1-5) if distant hills flicker; lower it if a
    // close-up object gets sliced open by the front of the frustum.
    float nearClip = 0.3f;      // nothing closer than this is drawn
    float farClip  = 25000.0f;  // nothing further than this is drawn
};

// ============================================================================
// HealthComponent: gives an entity hit points. Gameplay code calls
// scene.damage(entity, amount); when hp reaches zero the entity is destroyed.
// Attach it to anything that can be shot.
// ============================================================================
class HealthComponent : public Component {
public:
    const char* Name() const override { return "Health"; }
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<HealthComponent>(*this);
    }
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["hp"] = hp;  out["max"] = max;
    }
    void Deserialize(const nlohmann::json& in) override {
        hp  = in.value("hp", hp);
        max = in.value("max", max);
    }

    float hp  = 3.0f;   // current hit points
    float max = 3.0f;   // starting / maximum hit points
};

// ============================================================================
// ColliderComponent: describes the SHAPE an entity occupies for collision.
// ----------------------------------------------------------------------------
// A collider is not drawn in the game; it is an invisible volume used to answer
// the question "is something touching this object?". Gameplay code asks that
// through scene.hit(...), which finds the closest point on the collider and
// compares it to the shooter's reach.
//
// Three shapes cover almost everything:
//   * Sphere  - a ball. Cheapest to test. Good for round or blob-like things.
//   * Box     - a rectangular block ("OBB": it rotates with the entity, so it
//               is not axis-aligned). Good for buildings, crates, wings.
//   * Capsule - a cylinder with a half-sphere glued on each end, like a pill.
//               It is the standard shape for anything long and thin (a body, a
//               fuselage, a missile) because it has no sharp corners to snag on
//               and is still cheap to test.
//
// And one that is a different kind of thing entirely:
//   * Heightfield - the LANDSCAPE. Hills cannot be described by any of the
//               three volumes above: a box under the terrain is a flat lid at
//               one height, which is why an aircraft appears to fly straight
//               through the scenery. A heightfield is instead a grid of height
//               samples - a value for the ground level at each point - which
//               is both the natural description of terrain and far cheaper to
//               test against than the thousands of triangles that draw it.
//               It carries no size fields of its own: it reads the
//               TerrainComponent on the SAME ENTITY, so the collision surface
//               and the visible hills can never disagree. An entity without a
//               Terrain component gets nothing from it.
//               A heightfield is hollow and one-sided, so it can only be
//               STATIC scenery - it cannot itself be thrown around.
//
// Add a collider only to entities that should be hittable; an entity without
// one is invisible to collision queries. The editor draws the shape as a green
// wireframe in the viewport so it can be sized against the model.
// ============================================================================

// Which of the three volumes a ColliderComponent represents. The numbers are
// written into scene files, so never renumber existing entries - only append.
// Heightfield is unlike the other three: it carries no dimensions of its own.
// It means "collide against the TerrainComponent on this same entity", and
// takes its shape from that component's hills. See the note on it in
// ColliderComponent below.
enum class ColliderShape { Sphere = 0, Box = 1, Capsule = 2, Heightfield = 3 };

class ColliderComponent : public Component {
public:
    const char* Name() const override { return "Collider"; }
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<ColliderComponent>(*this);
    }
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        // The enum is stored as its underlying integer: JSON has no enums.
        out["shape"]       = static_cast<int>(shape);
        out["radius"]      = radius;
        out["height"]      = height;
        out["halfExtents"] = {halfExtents.x, halfExtents.y, halfExtents.z};
        out["offset"]      = {offset.x, offset.y, offset.z};
        out["rotation"]    = {rotation.x, rotation.y, rotation.z};
    }
    void Deserialize(const nlohmann::json& in) override {
        // in.value(key, fallback) returns the fallback when the key is absent,
        // so a file written by an older version still loads cleanly.
        int s  = in.value("shape", static_cast<int>(shape));
        // Clamp to the valid range: a corrupt or future file must not produce
        // an enum value none of our switches handle.
        if (s < 0 || s > 3) s = 0;
        shape  = static_cast<ColliderShape>(s);
        radius = in.value("radius", radius);
        height = in.value("height", height);
        if (in.contains("halfExtents"))
            halfExtents = {in["halfExtents"][0], in["halfExtents"][1], in["halfExtents"][2]};
        if (in.contains("offset"))
            offset = {in["offset"][0], in["offset"][1], in["offset"][2]};
        if (in.contains("rotation"))
            rotation = {in["rotation"][0], in["rotation"][1], in["rotation"][2]};
    }

    // The shape's own placement inside the entity: its rotation followed by its
    // offset, as a single 4x4 matrix. Both the collision maths and the editor
    // gizmo use this one function, so the volume that is tested is always
    // exactly the volume that is drawn.
    Matrix LocalMatrix() const;

    ColliderShape shape = ColliderShape::Sphere;

    // Sphere and Capsule: the radius of the ball / of the pill's round part.
    float radius = 1.0f;
    // Capsule only: the length of the straight middle section between the two
    // end caps, measured along the entity's local Y (up) axis. The capsule's
    // total length is therefore height + 2*radius.
    float height = 2.0f;
    // Box only: half the size on each axis, so a 4x2x6 block is {2, 1, 3}.
    // Half-extents are used instead of full sizes because every collision
    // formula wants the distance from the centre to a face, not the full width.
    Vector3 halfExtents{1.0f, 1.0f, 1.0f};

    // Where the shape sits relative to the entity's own origin, in the
    // entity's LOCAL space (so it rotates with the entity). Use it when the
    // model's pivot is not at its middle - e.g. a jet whose origin is at the
    // nose needs the collider pushed backwards along local -Z... or +Z,
    // depending on the model.
    Vector3 offset{0.0f, 0.0f, 0.0f};

    // How the shape is turned relative to the entity, in euler degrees (a
    // rotation about X, then Y, then Z). This is what lets a box lie along a
    // swept wing, or a capsule lie down the length of a fuselage instead of
    // standing upright - a capsule is built along its own Y axis, so a nose-to-
    // tail capsule on a -Z-facing aircraft needs X = 90 here. It is separate
    // from the entity's own rotation: the entity keeps facing where gameplay
    // points it, and only the collision volume is re-aimed.
    // A sphere looks the same from every angle, so this has no effect on one.
    Vector3 rotation{0.0f, 0.0f, 0.0f};
};

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

// ============================================================================
// LightComponent: makes an entity the scene's sun.
// ----------------------------------------------------------------------------
// A directional light has no position, only a direction, because it stands for
// something so distant (the sun) that its rays arrive parallel everywhere. This
// component takes that direction from the entity's own orientation - the light
// travels along the entity's FORWARD axis - so aiming the sun is the same as
// rotating any other object, and the arrow gizmo in the viewport shows where it
// points. The entity's position is ignored; only which way it faces matters.
//
// Only the FIRST light in the scene is used. Everything else here is colour.
// ============================================================================
class LightComponent : public Component {
public:
    const char* Name() const override { return "Light"; }
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<LightComponent>(*this);
    }
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["color"]     = {color.x, color.y, color.z};
        out["ambient"]   = {ambient.x, ambient.y, ambient.z};
        out["sky"]       = {sky.x, sky.y, sky.z};
        out["ground"]    = {ground.x, ground.y, ground.z};
        out["intensity"] = intensity;
    }
    void Deserialize(const nlohmann::json& in) override {
        if (in.contains("color"))   color   = {in["color"][0],   in["color"][1],   in["color"][2]};
        if (in.contains("ambient")) ambient = {in["ambient"][0], in["ambient"][1], in["ambient"][2]};
        if (in.contains("sky"))     sky     = {in["sky"][0],     in["sky"][1],     in["sky"][2]};
        if (in.contains("ground"))  ground  = {in["ground"][0],  in["ground"][1],  in["ground"][2]};
        intensity = in.value("intensity", intensity);
    }

    // Colour of the sunlight. Slightly warm by default (more red than blue),
    // which reads as daylight; a cold blue-white reads as moonlight. Kept
    // within 0..1 because that is the range a colour picker can show - to make
    // the light brighter than its own colour, raise the intensity below.
    Vector3 color{1.0f, 0.96f, 0.88f};
    // A plain multiplier on that colour, so brightness can be dialled without
    // changing the hue. Above 1 the light is stronger than the surface colour.
    float   intensity = 1.1f;
    // Light that reaches surfaces the sun cannot see. If this is zero, faces
    // turned away from the sun go pure black and lose all detail.
    Vector3 ambient{0.25f, 0.26f, 0.3f};
    // Faint tints for surfaces looking up (as if from the sky) and down (as if
    // bounced off the ground).
    Vector3 sky{0.05f, 0.07f, 0.12f};
    Vector3 ground{0.08f, 0.06f, 0.04f};
};

// Draws the entity as a loaded 3D MODEL (an .obj or .glb file) instead of a
// simple primitive. The model file is loaded lazily the first time it's drawn,
// and freed when the component is destroyed.
class ModelComponent : public Component {
public:
    ModelComponent() = default;
    // A loaded Model owns GPU resources that must be freed exactly once, so we
    // forbid copying this component (which would copy the handles and free them
    // twice). Clone() below makes a fresh, independent one instead.
    ModelComponent(const ModelComponent&) = delete;
    ModelComponent& operator=(const ModelComponent&) = delete;
    ~ModelComponent() override;

    const char* Name() const override { return "Model"; }

    // Clone copies only the path and tint; the new component loads its own copy
    // of the model on its first draw.
    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<ModelComponent>();
        c->path           = path;
        c->tint           = tint;
        c->rotationOffset = rotationOffset;
        c->positionOffset = positionOffset;
        c->scale          = scale;
        return c;
    }

    void OnDraw(const Entity& owner) override;
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["path"] = path;
        out["tint"] = {tint.r, tint.g, tint.b, tint.a};
        out["rotationOffset"] = {rotationOffset.x, rotationOffset.y, rotationOffset.z};
        out["positionOffset"] = {positionOffset.x, positionOffset.y, positionOffset.z};
        out["scale"] = {scale.x, scale.y, scale.z};
    }
    void Deserialize(const nlohmann::json& in) override {
        SetPath(in.value("path", path));
        if (in.contains("tint"))
            tint = {in["tint"][0], in["tint"][1], in["tint"][2], in["tint"][3]};
        if (in.contains("rotationOffset"))
            rotationOffset = {in["rotationOffset"][0], in["rotationOffset"][1], in["rotationOffset"][2]};
        if (in.contains("positionOffset"))
            positionOffset = {in["positionOffset"][0], in["positionOffset"][1], in["positionOffset"][2]};
        if (in.contains("scale"))
            scale = {in["scale"][0], in["scale"][1], in["scale"][2]};
    }

    // Change which file to draw (unloads any current model so the new one loads
    // on the next draw).
    void SetPath(const std::string& p);

    // How many triangles this model draws, summed over all its meshes. Zero
    // until the file has actually loaded. The editor totals these to show what
    // a scene costs to render.
    int TriangleCount() const;

    std::string path;             // the model file, e.g. "assets/models/jet.obj"
    Color       tint = WHITE;     // multiplied over the model's own colors
    // A fixed rotation (euler degrees) applied to the mesh when drawing, so a
    // model authored facing a different axis can be aligned to the engine's
    // -Z forward / +Y up convention without rotating the gameplay transform.
    Vector3     rotationOffset{0, 0, 0};

    // A fixed SHIFT (in the model's own units) applied to the mesh before that
    // rotation, used to move a model onto its entity's origin.
    //
    // Every model carries a "pivot": the point its coordinates are measured
    // from, chosen by whoever built it. Engines assume that point is at the
    // middle of the object, but exported models frequently put it somewhere
    // else entirely - at the nose, at a wingtip, or at the world origin of the
    // scene the model was authored in, which can be a long way off.
    //
    // That matters far beyond looking untidy, because EVERYTHING rotates about
    // the entity's origin. With the pivot outside the aircraft, turning the
    // entity swings the model around a point in mid-air like a ball on a
    // string, instead of banking it about its own centre. The collider, which
    // is placed sensibly around the origin, then no longer covers the visible
    // aircraft either.
    //
    // Setting this to the negative of the model's centre brings it back onto
    // the origin. The Inspector's "Centre On Origin" button works that out from
    // the model's bounding box rather than leaving it to be found by dragging.
    //
    // It is applied BEFORE the rotation offset, in the model's own frame, since
    // it describes where the mesh sits inside its own coordinates - so getting
    // the centring right once keeps working whatever the alignment rotation is
    // later set to.
    Vector3     positionOffset{0, 0, 0};

    // How much to resize the MODEL, without touching the entity.
    //
    // Model files disagree wildly about what one unit means - the same
    // helicopter may arrive 100 times too big or a hundredth of the size it
    // should be. The obvious fix is to scale the entity, but that is the wrong
    // knob: an entity's scale is what the rest of the game measures against.
    // Scaling it drags along every child, and it makes the entity claim to be a
    // size it is not, so a 0.01 entity reads as a centimetre-wide object when it
    // is really a helicopter that happens to have been exported large.
    //
    // Keeping this separate means the entity stays at its true size and only
    // the drawing is adjusted - which is exactly the distinction rotationOffset
    // and positionOffset already make.
    //
    // It is applied AFTER positionOffset, so that offset stays measured in the
    // model's own raw units - the same units the bounding box and the "Centre
    // On Origin" button work in - and scaling the model scales the shift with
    // it, instead of the two disagreeing.
    Vector3     scale{1.0f, 1.0f, 1.0f};

    // The model's bounding box in its own coordinates, and whether it is known
    // yet (it cannot be, until the file has actually loaded). The Inspector
    // reports it, which is what turns "the model is in the wrong place" from a
    // guess into a number.
    bool        Bounds(Vector3& outMin, Vector3& outMax) const;

private:
    void EnsureLoaded();          // load the file the first time we need it
    Model  m_model{};             // the loaded model (raylib type)
    Matrix m_baseTransform{};     // the model's own transform, captured at load
    bool   m_loaded = false;      // did it load successfully?
    bool   m_tried  = false;      // have we already attempted to load `path`?
};

// Procedurally generates and draws a 3D terrain mesh with rolling hills. The
// heights come from Perlin noise (a smooth, natural-looking random pattern),
// so you get real elevation to fly over instead of a flat plane. The mesh is
// built once (lazily) and rebuilt when you change the settings.
class TerrainComponent : public Component {
public:
    TerrainComponent() = default;
    // Owns a GPU mesh, so (like ModelComponent) it must not be copied.
    TerrainComponent(const TerrainComponent&) = delete;
    TerrainComponent& operator=(const TerrainComponent&) = delete;
    ~TerrainComponent() override;

    const char* Name() const override { return "Terrain"; }

    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<TerrainComponent>();
        c->worldSize  = worldSize;  c->maxHeight  = maxHeight;
        c->resolution = resolution; c->noiseScale = noiseScale;
        c->seed = seed; c->tint = tint; c->wire = wire;
        return c;
    }

    void OnDraw(const Entity& owner) override;
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["worldSize"]  = worldSize;  out["maxHeight"]  = maxHeight;
        out["resolution"] = resolution; out["noiseScale"] = noiseScale;
        out["seed"] = seed;
        out["tint"] = {tint.r, tint.g, tint.b, tint.a};
        out["wire"] = wire;
    }
    void Deserialize(const nlohmann::json& in) override {
        worldSize  = in.value("worldSize", worldSize);
        maxHeight  = in.value("maxHeight", maxHeight);
        resolution = in.value("resolution", resolution);
        noiseScale = in.value("noiseScale", noiseScale);
        seed       = in.value("seed", seed);
        if (in.contains("tint"))
            tint = {in["tint"][0], in["tint"][1], in["tint"][2], in["tint"][3]};
        wire = in.value("wire", wire);
        Rebuild();
    }

    // Settings you can tweak in the Inspector (press Regenerate to apply).
    float worldSize  = 400.0f;      // how many world units wide/deep the terrain is
    float maxHeight  = 25.0f;       // height of the tallest hills
    int   resolution = 80;          // grid detail (more = smoother, heavier)
    float noiseScale = 5.0f;        // hill frequency (higher = more, smaller hills)
    int   seed       = 0;           // change for a different random landscape
    Color tint       = DARKGREEN;
    bool  wire       = true;        // overlay contour lines so the hills read clearly

    void Rebuild();                 // discard the mesh so it regenerates next draw

    // The terrain's height at each point of an n x n grid, as a value from 0
    // (lowest) to 1 (the full maxHeight), laid out row by row so that the
    // sample for grid position (x, z) is at index z * n + x.
    //
    // This exists so the physics engine can build a collision surface from the
    // same landscape that is drawn, without needing to know anything about
    // Perlin noise or raylib images. It re-derives the heights from the
    // settings rather than reading the built mesh, so it works even before the
    // terrain has ever been drawn - which matters because collision bodies are
    // created when play starts, and the Game view may not have drawn yet.
    //
    // `n` need not match `resolution`: the grid is sampled smoothly, so the
    // collision surface can be coarser than the visible mesh. It is a separate
    // number because the physics engine constrains what sizes it will accept.
    std::vector<float> SampleHeights(int n) const;

    // How many triangles the terrain mesh is made of. The heightmap is turned
    // into a grid of quads - one per group of four neighbouring pixels - and
    // each quad is two triangles, so the count grows with the SQUARE of the
    // resolution: doubling it makes four times the geometry. This is computed
    // from the settings, so it answers "what would this cost?" even before the
    // mesh has been built.
    int TriangleCount() const {
        int cells = (resolution > 1) ? (resolution - 1) : 0;
        return cells * cells * 2;
    }

private:
    void EnsureBuilt();
    Model m_model{};
    bool  m_built = false;
    bool  m_tried = false;
};

// Component "factory": given a type name read from a save file (a string like
// "Shape"), build the matching component object. This is how loading turns
// text back into real C++ objects. Returns nullptr for names it doesn't know,
// so an unfamiliar entry in a file is skipped rather than crashing the load.
std::unique_ptr<Component> MakeComponent(const std::string& name);

// Free every model file the engine has loaded. Model files are loaded once and
// shared by everything that draws them (see the cache note in Components.cpp),
// and are kept until this is called. Call ONCE at shutdown, while the window -
// and therefore the graphics device - still exists.
void ClearModelCache();

// Load a model file into that shared cache now, rather than when something
// first draws it. Use it to move an unavoidable cost to a moment where a pause
// is expected - pressing Play - instead of mid-game when a wave spawns.
// Does nothing if the file is already loaded or cannot be read.
void PreloadModel(const std::string& path);

// ============================================================================
// MinimapComponent: a player-centred radar, drawn over the game view.
// ----------------------------------------------------------------------------
// Attach it to the entity it should be centred on - the player's aircraft. It
// draws in SCREEN space through OnDrawHud, not in the world, so it appears in
// the Game view over the finished 3D image.
//
// WHY IT IS DRAWN RATHER THAN RENDERED. The obvious way to build a minimap is a
// second camera looking straight down into a texture, which is what large
// engines offer. That costs a COMPLETE extra pass over the scene, and almost all
// of it is wasted here: at minimap size an aircraft is smaller than a pixel, so
// the only thing that pass really contributes is the landscape - which this
// engine can already produce as data, without drawing anything. So the terrain
// is baked into a small image ONCE and aircraft are drawn as symbols on top.
// That is also what a radar in a real aircraft is: symbols, not a picture.
//
// HEADING-UP. The map turns so the aircraft always points up the screen, the
// convention for anything you sit inside. It means a contact drawn above you is
// ahead of you, which is the question being asked mid-dogfight. The cost is that
// north moves, so a small tick marks where north has gone.
// ============================================================================
class MinimapComponent : public Component {
public:
    ~MinimapComponent() override;

    const char* Name() const override { return "Minimap"; }

    // Copied field by field rather than with the compiler's own copy, because
    // this component owns a TEXTURE. A blind copy would duplicate that handle
    // and both copies would later free the same image. The clone starts with no
    // image and bakes its own on first use.
    std::unique_ptr<Component> Clone() const override;

    void OnInspector() override;
    void OnDrawHud(const Entity& owner, int width, int height) override;

    void Serialize(nlohmann::json& out) const override {
        out["range"] = range;   out["size"]    = size;
        out["corner"] = corner; out["terrain"] = showTerrain;
        out["tag"] = blipTag;
    }
    void Deserialize(const nlohmann::json& in) override {
        range       = in.value("range",  range);
        size        = in.value("size",   size);
        corner      = in.value("corner", corner);
        showTerrain = in.value("terrain", showTerrain);
        blipTag     = in.value("tag", blipTag);
    }

    // How far the radar reaches, in metres. Contacts beyond this are held at the
    // rim rather than dropped, so a threat never simply vanishes.
    float range = 5000.0f;
    int   size  = 200;          // width and height on screen, in pixels
    int   corner = 3;           // 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right
    bool  showTerrain = true;   // draw the baked landscape behind the contacts
    std::string blipTag = "enemy";   // which tag counts as a contact

private:
    // Bake the landscape into an image, once. Returns false when there is no
    // terrain in the scene, in which case the radar simply has no backdrop.
    bool EnsureTerrain();

    Texture2D m_terrain{};      // the baked relief image
    bool  m_built = false;      // has it been baked?
    bool  m_tried = false;      // has baking been ATTEMPTED? (so a failure is not retried every frame)
    float m_worldSize = 0.0f;   // the terrain's span in metres, needed to map world to pixel
    Vector3 m_worldCentre{};    // where the terrain's middle sits in the world
};

// A tiny shared store of named numbers that scripts can post to (via the Lua
// `hud.set(name, value)` call) and the editor's HUD can read back. This is how
// a value that lives inside a Lua script (like throttle) reaches the C++ HUD.
// --- HUD drawing support (used by the `draw.*` script API) -------------------

// Named colours for HUD drawing. A script may add to the palette with
// draw.defineColor, so a HUD's colours are data rather than compiled in.
// An unknown name resolves to the default HUD green rather than failing.
void  DefineHudColor(const std::string& name, Color c);
Color HudColor(const std::string& name);   // empty name = the default HUD colour

// Whether HUD drawing is currently legal. The `draw.*` calls only work inside
// the HUD pass; from any other hook they do nothing, because pixel coordinates
// mean nothing in the middle of the 3D pass. Anything that dispatches OnDrawHud
// must bracket it with these.
bool HudDrawAllowed();
void BeginHudPass();
void EndHudPass();

void  SetHudValue(const std::string& key, float value);
float GetHudValue(const std::string& key, float fallback = 0.0f);
// Forget every published HUD value. Called when play starts so each run begins
// fresh (score back to 0, no stale throttle/speed left from the last run).
void  ClearHudValues();

// Global on/off switch for the scripting `input` API. The editor turns it OFF
// while you are typing in a text box or flying the editor's own camera, so the
// game doesn't react to keystrokes meant for the editor. A shipped game leaves
// it on. Defined in the .cpp.
void SetScriptInputEnabled(bool enabled);

} // namespace eng
