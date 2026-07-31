#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
// ============================================================================
// HealthComponent: gives an entity hit points. Gameplay code calls
// scene.damage(entity, amount); when hp reaches zero the entity is destroyed.
// Attach it to anything that can be shot.
// ============================================================================
class HealthComponent : public Component {
public:
    const char* Name() const override { return "Health"; }
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<HealthComponent>(*this);
    }
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["hp"] = hp;  out["max"] = max;
    }
    void Deserialize(const nlohmann::json& in) override {
        hp  = in.value("hp", hp);
        max = in.value("max", max);
    }

    float hp  = 3.0f;   // current hit points
    float max = 3.0f;   // starting / maximum hit points
};
} // namespace eng
