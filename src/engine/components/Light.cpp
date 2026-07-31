#include "engine/components/Light.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "engine/Lighting.h"

namespace eng {
void LightComponent::OnInspector() {
    // ColorEdit3 shows a colour swatch that opens a picker. It works on three
    // floats in 0..1 order R, G, B - the same layout as a Vector3 - so the
    // address of the first field can be handed to it directly.
    ImGui::ColorEdit3("Color", &color.x);
    // Brightness is separate from hue so the sun can be dimmed for a dusk look
    // without turning it a different colour.
    ImGui::DragFloat("Intensity", &intensity, 0.02f, 0.0f, 8.0f);
    ImGui::ColorEdit3("Ambient", &ambient.x);
    ImGui::ColorEdit3("Sky Fill", &sky.x);
    ImGui::ColorEdit3("Ground Fill", &ground.x);
    // The direction is not edited here: it comes from the entity's rotation, so
    // the light is aimed with the same Rotation fields as any other object.
    ImGui::TextDisabled("Direction = the entity's forward axis");
    ImGui::TextDisabled("Rotate the entity to aim the sun.");
}

} // namespace eng
