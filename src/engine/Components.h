#pragma once

#include "engine/Component.h"   // the Component base class we derive from

#include "raylib.h"        // Color, Camera3D, Matrix, drawing types
#include "sol/sol.hpp"     // sol2: a C++ wrapper that runs Lua and binds C++ to it

#include <string>

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

    // Copying only carries over the file PATH, not the live Lua state (which
    // is runtime-only). The fresh copy starts unloaded and loads itself when
    // play begins.
    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<ScriptComponent>();
        c->path = path;
        return c;
    }

    // Lifecycle hooks (bodies are in the .cpp). OnStart loads the file and
    // calls the script's on_start; OnUpdate calls its on_update each frame;
    // OnDestroy calls its on_destroy.
    void OnStart(Entity& owner) override;
    void OnDestroy(Entity& owner) override;
    void OnUpdate(float dt, Entity& owner) override;

    // Save/load which script file this component points at.
    void Serialize(nlohmann::json& out) const override { out["path"] = path; }
    void Deserialize(const nlohmann::json& in) override {
        // .value(key, fallback) returns the stored value, or `fallback` if the
        // key is missing. Using it means old save files (that lack newer keys)
        // still load instead of throwing an error.
        path = in.value("path", path);
    }

    // Draws the path field + a Load/Reload button + any error text.
    void OnInspector() override;

    // (Re)read the .lua file into a fresh Lua state. Errors do NOT crash the
    // program: they are captured into m_error and shown in the Inspector.
    void Load();

    // The script file to run. Public so the editor and spawn code can set it.
    std::string path = "assets/scripts/spin.lua";

private:
    sol::state m_lua;                       // this script's private Lua interpreter
    // Handles to the script's optional functions. A sol::protected_function
    // can be called safely: if the Lua code errors, we get an error result
    // instead of a crash.
    sol::protected_function m_onStart;
    sol::protected_function m_onUpdate;
    sol::protected_function m_onDestroy;
    bool        m_loaded = false;           // did the file load without error?
    std::string m_error;                    // last error message, "" if none
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

    void Serialize(nlohmann::json& out) const override { out["fovy"] = fovy; }
    void Deserialize(const nlohmann::json& in) override { fovy = in.value("fovy", fovy); }

    // Turn an entity's world matrix (from Scene::WorldMatrix) into the
    // Camera3D struct that raylib needs to render a 3D view. Doing it from the
    // world matrix is what lets a camera parented to the jet follow it.
    Camera3D ToCamera3D(const Matrix& world) const;

    float fovy = 60.0f;         // vertical field of view in degrees (zoom)
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
        c->path = path;
        c->tint = tint;
        return c;
    }

    void OnDraw(const Entity& owner) override;
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["path"] = path;
        out["tint"] = {tint.r, tint.g, tint.b, tint.a};
    }
    void Deserialize(const nlohmann::json& in) override {
        SetPath(in.value("path", path));
        if (in.contains("tint"))
            tint = {in["tint"][0], in["tint"][1], in["tint"][2], in["tint"][3]};
    }

    // Change which file to draw (unloads any current model so the new one loads
    // on the next draw).
    void SetPath(const std::string& p);

    std::string path;             // the model file, e.g. "assets/models/jet.obj"
    Color       tint = WHITE;     // multiplied over the model's own colors

private:
    void EnsureLoaded();          // load the file the first time we need it
    Model m_model{};              // the loaded model (raylib type)
    bool  m_loaded = false;       // did it load successfully?
    bool  m_tried  = false;       // have we already attempted to load `path`?
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

// Global on/off switch for the scripting `input` API. The editor turns it OFF
// while you are typing in a text box or flying the editor's own camera, so the
// game doesn't react to keystrokes meant for the editor. A shipped game leaves
// it on. Defined in the .cpp.
void SetScriptInputEnabled(bool enabled);

} // namespace eng
