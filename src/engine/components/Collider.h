#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
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
} // namespace eng
