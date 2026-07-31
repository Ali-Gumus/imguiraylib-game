#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
// ============================================================================
// LightComponent: makes an entity the scene's sun.
// ----------------------------------------------------------------------------
// A directional light has no position, only a direction, because it stands for
// something so distant (the sun) that its rays arrive parallel everywhere. This
// component takes that direction from the entity's own orientation - the light
// travels along the entity's FORWARD axis - so aiming the sun is the same as
// rotating any other object, and the arrow gizmo in the viewport shows where it
// points. The entity's position is ignored; only which way it faces matters.
//
// Only the FIRST light in the scene is used. Everything else here is colour.
// ============================================================================
class LightComponent : public Component {
public:
    const char* Name() const override { return "Light"; }
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<LightComponent>(*this);
    }
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["color"]     = {color.x, color.y, color.z};
        out["ambient"]   = {ambient.x, ambient.y, ambient.z};
        out["sky"]       = {sky.x, sky.y, sky.z};
        out["ground"]    = {ground.x, ground.y, ground.z};
        out["intensity"] = intensity;
    }
    void Deserialize(const nlohmann::json& in) override {
        if (in.contains("color"))   color   = {in["color"][0],   in["color"][1],   in["color"][2]};
        if (in.contains("ambient")) ambient = {in["ambient"][0], in["ambient"][1], in["ambient"][2]};
        if (in.contains("sky"))     sky     = {in["sky"][0],     in["sky"][1],     in["sky"][2]};
        if (in.contains("ground"))  ground  = {in["ground"][0],  in["ground"][1],  in["ground"][2]};
        intensity = in.value("intensity", intensity);
    }

    // Colour of the sunlight. Slightly warm by default (more red than blue),
    // which reads as daylight; a cold blue-white reads as moonlight. Kept
    // within 0..1 because that is the range a colour picker can show - to make
    // the light brighter than its own colour, raise the intensity below.
    Vector3 color{1.0f, 0.96f, 0.88f};
    // A plain multiplier on that colour, so brightness can be dialled without
    // changing the hue. Above 1 the light is stronger than the surface colour.
    float   intensity = 1.1f;
    // Light that reaches surfaces the sun cannot see. If this is zero, faces
    // turned away from the sun go pure black and lose all detail.
    Vector3 ambient{0.25f, 0.26f, 0.3f};
    // Faint tints for surfaces looking up (as if from the sky) and down (as if
    // bounced off the ground).
    Vector3 sky{0.05f, 0.07f, 0.12f};
    Vector3 ground{0.08f, 0.06f, 0.04f};
};
} // namespace eng
