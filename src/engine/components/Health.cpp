#include "engine/components/Health.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eng {
void HealthComponent::OnInspector() {
    // ImGui::DragFloat draws a number you can click-drag to change. Arguments:
    // label, pointer to the value, drag speed, minimum, maximum.
    ImGui::DragFloat("HP",  &hp,  0.1f, 0.0f, 10000.0f);
    ImGui::DragFloat("Max", &max, 0.1f, 1.0f, 10000.0f);
}

} // namespace eng
