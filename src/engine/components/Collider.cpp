#include "engine/components/Collider.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eng {
Matrix ColliderComponent::LocalMatrix() const {
    // Degrees are friendly to type in the Inspector; the maths needs radians.
    Matrix rot = MatrixRotateXYZ({rotation.x * DEG2RAD,
                                  rotation.y * DEG2RAD,
                                  rotation.z * DEG2RAD});
    Matrix move = MatrixTranslate(offset.x, offset.y, offset.z);
    // MatrixMultiply(A, B) means "apply A, then B" for the way raylib
    // transforms points. Rotating first and moving second spins the shape
    // about its OWN centre and then puts it in place; doing it the other way
    // round would swing the shape around the entity's origin instead.
    return MatrixMultiply(rot, move);
}

void ColliderComponent::OnInspector() {
    // The order of these strings must match the ColliderShape enum, because
    // ImGui::Combo works on the INDEX of the selected item.
    static const char* kShapeNames[] = { "Sphere", "Box", "Capsule", "Heightfield" };

    // ImGui::Combo wants an int it can write into, so convert to and from the
    // enum around the call.
    int current = static_cast<int>(shape);
    if (ImGui::Combo("Shape", &current, kShapeNames, IM_ARRAYSIZE(kShapeNames)))
        shape = static_cast<ColliderShape>(current);

    // Only show the fields the chosen shape actually uses, so the panel never
    // presents a number that does nothing.
    switch (shape) {
        case ColliderShape::Sphere:
            ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 1000.0f);
            break;
        case ColliderShape::Box:
            // DragFloat3 edits three floats at once; &halfExtents.x points at
            // the first of the three, which sit next to each other in memory.
            ImGui::DragFloat3("Half Extents", &halfExtents.x, 0.05f, 0.0f, 1000.0f);
            break;
        case ColliderShape::Capsule:
            ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 1000.0f);
            // The straight part between the two round caps; the pill's total
            // length is this plus twice the radius.
            ImGui::DragFloat("Height", &height, 0.05f, 0.0f, 1000.0f);
            break;
        case ColliderShape::Heightfield:
            // Nothing to size: the shape IS the terrain on this entity, so
            // say that rather than showing an empty panel. (Whether such a
            // terrain actually exists is checked by the Inspector, which can
            // see the whole entity; a component only sees itself.)
            ImGui::TextDisabled("Shaped by the Terrain component\non this entity.");
            break;
    }

    // Applies to every shape: shifts the volume away from the entity's origin.
    ImGui::DragFloat3("Offset", &offset.x, 0.05f, -1000.0f, 1000.0f);

    // A sphere is the same shape whichever way you turn it, so only offer the
    // rotation where it can actually change something.
    if (shape != ColliderShape::Sphere) {
        // Drag speed 1.0 = one degree per pixel dragged, matching the other
        // rotation fields in the editor.
        ImGui::DragFloat3("Rotation", &rotation.x, 1.0f);
        // A capsule stands along its own Y axis. Aircraft in this engine face
        // local -Z, so the common case of a nose-to-tail capsule is X = 90.
        if (shape == ColliderShape::Capsule && ImGui::Button("Lay Along Forward"))
            rotation = {90.0f, 0.0f, 0.0f};
    }
}

} // namespace eng
