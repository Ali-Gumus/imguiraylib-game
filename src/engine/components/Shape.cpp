#include "engine/components/Shape.h"

// Shape checks for a Model on the same entity - a primitive should not draw
// through a mesh that is already there. That dependency was invisible while
// every component shared one file.
#include "engine/components/Model.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eng {
// ---- ShapeComponent --------------------------------------------------------

void ShapeComponent::OnDraw(const Entity& owner) {
    // A model replaces the primitive. Spawned entities are given a cube so that
    // something is always visible, and a model may be added on top of it; drawing
    // both would leave a cube buried inside every aircraft. Skipping here rather
    // than removing the cube component avoids changing an entity's component
    // list while hooks are running over it (see the note in Scene::Start).
    if (owner.GetComponent<ModelComponent>() != nullptr) return;

    // Before calling this, Scene::Draw pushed this entity's world matrix onto
    // raylib's matrix stack. That matrix already encodes position, rotation
    // and scale (including parents'), so here we simply draw a unit-sized
    // primitive centered at the origin and it appears in the right place.
    switch (kind) {
        case Kind::Cube:
            DrawCube({0, 0, 0}, 1.0f, 1.0f, 1.0f, tint);
            if (wireframe) DrawCubeWires({0, 0, 0}, 1.0f, 1.0f, 1.0f, BLACK);
            break;
        case Kind::Sphere:
            DrawSphere({0, 0, 0}, 0.5f, tint);
            if (wireframe) DrawSphereWires({0, 0, 0}, 0.5f, 12, 12, BLACK);
            break;
        case Kind::Cylinder:   // raylib builds cylinders upward from the base,
                               // so we offset down by half to center it
            DrawCylinder({0, -0.5f, 0}, 0.5f, 0.5f, 1.0f, 16, tint);
            if (wireframe) DrawCylinderWires({0, -0.5f, 0}, 0.5f, 0.5f, 1.0f, 16, BLACK);
            break;
        case Kind::Cone:       // a cone is a cylinder whose top radius is 0
            DrawCylinder({0, -0.5f, 0}, 0.0f, 0.5f, 1.0f, 16, tint);
            if (wireframe) DrawCylinderWires({0, -0.5f, 0}, 0.0f, 0.5f, 1.0f, 16, BLACK);
            break;
        case Kind::Plane:      // a flat square; raylib has no wireframe plane
            DrawPlane({0, 0, 0}, {1.0f, 1.0f}, tint);
            break;
    }
}

void ShapeComponent::OnInspector() {
    // A dropdown to pick the shape. The names array order must match the Kind
    // enum order, because the dropdown works with the integer index.
    static const char* kKindNames[] = {"Cube", "Sphere", "Cylinder", "Cone", "Plane"};
    int k = (int)kind;
    // Labeled "Type", not "Shape", so its ImGui id doesn't collide with the
    // component's "Shape" header just above it.
    if (ImGui::Combo("Type", &k, kKindNames, 5))
        kind = (Kind)k;

    // ImGui color pickers use four floats in the 0..1 range; raylib's Color
    // uses four bytes in 0..255. Convert one way in, the other way out.
    float col[4] = {tint.r / 255.0f, tint.g / 255.0f,
                    tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", col)) {
        tint = {(unsigned char)(col[0] * 255), (unsigned char)(col[1] * 255),
                (unsigned char)(col[2] * 255), (unsigned char)(col[3] * 255)};
    }
    ImGui::Checkbox("Wireframe", &wireframe);
}

} // namespace eng
