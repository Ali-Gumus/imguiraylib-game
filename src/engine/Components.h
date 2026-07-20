#pragma once

#include "engine/Component.h"

#include "raylib.h"
#include "sol/sol.hpp"

#include <string>

namespace eng {

// Runs a Lua file's hooks on its entity every frame. Each component has
// its OWN Lua state: scripts can't trample each other's globals, and
// reloading one script never disturbs another.
class ScriptComponent : public Component {
public:
    const char* Name() const override { return "Script"; }
    // Several scripts on one entity is useful (movement + health + ...),
    // exactly like multiple MonoBehaviours in Unity.
    bool AllowMultiple() const override { return true; }

    // Clone copies only the path — the Lua state is runtime baggage, the
    // fresh copy starts unloaded. OnStart loads it when play begins.
    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<ScriptComponent>();
        c->path = path;
        return c;
    }
    void OnStart(Entity& owner) override;     // loads script, calls on_start
    void OnDestroy(Entity& owner) override;   // calls on_destroy

    void Serialize(nlohmann::json& out) const override { out["path"] = path; }
    void Deserialize(const nlohmann::json& in) override {
        // .value(key, default): missing keys fall back instead of throwing —
        // old scene files stay loadable when components gain new fields.
        path = in.value("path", path);
    }

    void OnUpdate(float dt, Entity& owner) override;
    void OnInspector() override;

    // (Re)load `path` into a fresh Lua state. Errors don't throw — they
    // land in m_error and show in the Inspector instead of crashing.
    void Load();

    std::string path = "assets/scripts/spin.lua";

private:
    sol::state              m_lua;
    // The script's lifecycle hooks — each is optional in the .lua file.
    sol::protected_function m_onStart;
    sol::protected_function m_onUpdate;
    sol::protected_function m_onDestroy;
    bool        m_loaded = false;
    std::string m_error;
};

// Renders the entity as a colored primitive (placeholder until 3D models).
// The proof that looks are optional: no ShapeComponent = invisible entity.
class ShapeComponent : public Component {
public:
    // Keep the order stable: the enum's int value goes into scene files.
    enum class Kind { Cube = 0, Sphere, Cylinder, Cone, Plane };

    const char* Name() const override { return "Shape"; }
    // Plain data (color + bool) — the compiler-written copy is correct.
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<ShapeComponent>(*this);
    }
    void OnDraw(const Entity& owner) override;
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["kind"]      = (int)kind;
        out["tint"]      = {tint.r, tint.g, tint.b, tint.a};
        out["wireframe"] = wireframe;
    }
    void Deserialize(const nlohmann::json& in) override {
        kind = (Kind)in.value("kind", 0);   // old files: no key -> Cube
        if (in.contains("tint"))
            tint = {in["tint"][0], in["tint"][1], in["tint"][2], in["tint"][3]};
        wireframe = in.value("wireframe", wireframe);
    }

    Kind  kind = Kind::Cube;
    Color tint = MAROON;
    bool  wireframe = true;           // draw the black edge lines?
};

// Makes its entity a camera: the Game view (and later, the shipped game)
// renders through this. Position/rotation come from the entity transform,
// so scripts moving the entity move the camera — that's the whole point.
class CameraComponent : public Component {
public:
    const char* Name() const override { return "Camera"; }
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<CameraComponent>(*this);
    }
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override { out["fovy"] = fovy; }
    void Deserialize(const nlohmann::json& in) override { fovy = in.value("fovy", fovy); }

    // Build the raylib camera from the entity's WORLD matrix (ask
    // Scene::WorldMatrix) — so a camera parented to the player follows it.
    Camera3D ToCamera3D(const Matrix& world) const;

    float fovy = 60.0f;               // vertical field of view, degrees
};

// The component REGISTRY: turns a type name from a scene file back into
// a real object. Loading is why it must exist — a file says "Shape" (a
// string) and someone has to know which class that means.
// Returns nullptr for unknown names (old/foreign files degrade gracefully).
std::unique_ptr<Component> MakeComponent(const std::string& name);

// Master switch for the script `input` API. The EDITOR closes it while
// the user is typing in a text field or flying the editor camera, so
// gameplay doesn't react to keystrokes meant for the UI. (In a shipped
// game nobody turns it off — default is on.)
void SetScriptInputEnabled(bool enabled);

} // namespace eng
