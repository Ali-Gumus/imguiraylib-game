#pragma once

#include "engine/Component.h"

#include "raylib.h"

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace eng {

// Position/rotation/scale of an entity in 3D world space.
// (raylib already has a "Transform" type; the suffix avoids the clash.)
struct Transform3D {
    Vector3 position{0.0f, 0.0f, 0.0f};
    Vector3 rotation{0.0f, 0.0f, 0.0f};   // euler angles, degrees
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

using EntityID = std::uint32_t;
inline constexpr EntityID kInvalidEntity = 0;   // id 0 = "no entity"

// A thing in the world: id + name + transform, everything else is an
// optional Component (Unity's GameObject model). Transform is built in
// on purpose — literally everything needs a position.
struct Entity {
    EntityID    id = kInvalidEntity;
    std::string name;
    Transform3D transform;

    // unique_ptr: components are polymorphic (storing them by value in a
    // vector would "slice" off the derived part) and the entity owns them.
    std::vector<std::unique_ptr<Component>> components;

    // Add: eng::ShapeComponent& s = e.AddComponent<ShapeComponent>();
    template <typename T, typename... Args>
    T& AddComponent(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *comp;
        components.push_back(std::move(comp));
        return ref;
    }

    // Deep copy: same id/name/transform, components cloned one by one.
    // Ids are preserved so selection survives a play/stop round-trip.
    Entity Clone() const {
        Entity c;
        c.id        = id;
        c.name      = name;
        c.transform = transform;
        for (const auto& comp : components)
            c.components.push_back(comp->Clone());
        return c;
    }

    // nullptr if the entity has no component of that type.
    // dynamic_cast = "is this base pointer actually a T?" — simple and
    // readable; engines optimize this later, we don't need to yet.
    template <typename T>
    T* GetComponent() {
        for (auto& c : components)
            if (T* t = dynamic_cast<T*>(c.get())) return t;
        return nullptr;
    }
};

// Owns all entities in a world. Refer to entities by id, never by
// pointer: the vector reallocates as it grows, invalidating pointers.
class Scene {
public:
    // Creates an entity at the world origin and returns its id.
    EntityID CreateEntity(const std::string& name);
    void     DestroyEntity(EntityID id);

    // nullptr if the id doesn't exist (e.g. entity was destroyed).
    // The pointer is only valid until the entity list next changes!
    Entity* Find(EntityID id);

    // Write/read the whole scene to a JSON file. Save returns false on
    // I/O failure; Load returns false on I/O or parse failure (and
    // leaves the current scene untouched in that case).
    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

    // Fire every component's OnStart — call once when play mode begins.
    void Start();

    // Advance the world one tick: every component's OnUpdate runs.
    void Update(float dt);

    // Render every entity. Caller must be inside BeginMode3D/EndMode3D —
    // the scene draws the world; who looks at it (the camera) is not its job.
    void Draw() const;

    std::vector<Entity>&       Entities()       { return m_entities; }
    const std::vector<Entity>& Entities() const { return m_entities; }

private:
    std::vector<Entity> m_entities;
    EntityID m_nextID = 1;            // ids start at 1; 0 means invalid
};

} // namespace eng
