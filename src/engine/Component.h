#pragma once

#include <nlohmann/json.hpp>

#include <memory>

namespace eng {

struct Entity;   // forward declaration — Component.h must not depend on Scene.h

// Base class for everything attachable to an entity (Unity's "Component").
// An entity is just id + name + transform; ALL optional behavior/looks
// (rendering, physics, scripts...) derive from this.
class Component {
public:
    virtual ~Component() = default;

    // Shown as the section header in the Inspector.
    virtual const char* Name() const = 0;

    // May an entity carry several of this component? (Unity's
    // DisallowMultipleComponent, inverted.) Default: one is enough.
    virtual bool AllowMultiple() const { return false; }

    // Deep-copy yourself. Pure virtual ON PURPOSE: every component must
    // answer "how am I copied?" — play mode snapshots the scene with this.
    virtual std::unique_ptr<Component> Clone() const = 0;

    // Write/read your DATA (not runtime state — same call as Clone) to a
    // JSON object. The scene stores it under your Name(), so components
    // never collide with each other's keys.
    virtual void Serialize(nlohmann::json& out) const {}
    virtual void Deserialize(const nlohmann::json& in) {}

    // Called once when play mode starts (Unity's Start()).
    virtual void OnStart(Entity& owner) {}

    // Called when the entity is destroyed (Unity's OnDestroy()).
    virtual void OnDestroy(Entity& owner) {}

    // Called every frame before rendering — behavior/logic lives here.
    virtual void OnUpdate(float dt, Entity& owner) {}

    // Called every frame inside BeginMode3D — draw yourself if visual.
    virtual void OnDraw(const Entity& owner) {}

    // Draw your own editing UI (ImGui calls). The Inspector calls this
    // for each component — it never needs to know concrete types.
    virtual void OnInspector() {}
};

} // namespace eng
