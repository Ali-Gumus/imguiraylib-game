#include "engine/components/RigidBody.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "engine/Physics.h"

namespace eng {
// ---- RigidBodyComponent ----------------------------------------------------

void RigidBodyComponent::OnInspector() {
    // The order of these strings must match the MotionType enum, because
    // ImGui::Combo works on the INDEX of the selected item.
    static const char* kMotionNames[] = { "Static", "Kinematic", "Dynamic" };

    int current = static_cast<int>(motion);
    if (ImGui::Combo("Motion", &current, kMotionNames, IM_ARRAYSIZE(kMotionNames)))
        motion = static_cast<MotionType>(current);

    // A one-line reminder of what the chosen type means, because picking the
    // wrong one produces behaviour that looks like a bug rather than a setting
    // (a "static" aircraft simply refuses to move, with no error anywhere).
    switch (motion) {
        case MotionType::Static:
            ImGui::TextDisabled("Never moves. For ground and scenery.");
            break;
        case MotionType::Kinematic:
            ImGui::TextDisabled("Moved by scripts; pushes dynamic bodies.");
            break;
        case MotionType::Dynamic:
            ImGui::TextDisabled("Moved by gravity, forces and collisions.");
            break;
    }

    // Everything below this point only affects a body the simulation actually
    // integrates, so hide it for a static one rather than offering numbers
    // that do nothing.
    if (motion == MotionType::Static) return;

    // Mass must stay above zero: dividing a force by zero mass is an infinite
    // acceleration, which turns the body's position into "not a number" and
    // then quietly corrupts everything it touches.
    ImGui::DragFloat("Mass (kg)", &mass, 0.5f, 0.001f, 100000.0f);

    ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Gravity Factor", &gravityFactor, 0.05f, -5.0f, 5.0f);
    ImGui::DragFloat3("Start Velocity", &initialVelocity.x, 0.5f);

    ImGui::Checkbox("Continuous collision", &continuous);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sweep the whole path each step instead of only\n"
                          "testing where the body lands. Needed for anything\n"
                          "fast enough to jump past a wall between steps -\n"
                          "bullets above all. Costs more, so leave it off\n"
                          "for ordinary objects.");
}

} // namespace eng
