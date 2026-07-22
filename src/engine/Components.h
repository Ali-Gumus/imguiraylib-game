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
