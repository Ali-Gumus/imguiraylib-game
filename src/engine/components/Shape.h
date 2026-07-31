#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
// ============================================================================
// ShapeComponent: draws the entity as a simple colored 3D primitive.
// An entity with no ShapeComponent (and no other visual) is invisible but
// still exists in the world.
// ============================================================================
class ShapeComponent : public Component {
public:
    // The primitive shapes we can draw. `enum class` is a strongly-typed
    // enumeration. The explicit "= 0" fixes Cube's number; the rest follow
    // (Sphere=1, ...). Those numbers are written into save files, so the
    // order must stay stable or old scenes would load the wrong shape.
    enum class Kind { Cube = 0, Sphere, Cylinder, Cone, Plane };

    const char* Name() const override { return "Shape"; }

    // This component is plain data (an enum, a color, a bool), so the
    // compiler-generated copy constructor (*this) copies it correctly.
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<ShapeComponent>(*this);
    }

    void OnDraw(const Entity& owner) override;      // draw the primitive
    void OnInspector() override;                    // shape/color editing UI

    void Serialize(nlohmann::json& out) const override {
        out["kind"]      = (int)kind;               // store the enum as its number
        out["tint"]      = {tint.r, tint.g, tint.b, tint.a};   // color as RGBA
        out["wireframe"] = wireframe;
    }
    void Deserialize(const nlohmann::json& in) override {
        kind = (Kind)in.value("kind", 0);           // missing -> Cube (0)
        if (in.contains("tint"))
            tint = {in["tint"][0], in["tint"][1], in["tint"][2], in["tint"][3]};
        wireframe = in.value("wireframe", wireframe);
    }

    Kind  kind = Kind::Cube;    // which primitive to draw
    Color tint = MAROON;        // its color (MAROON is a raylib preset)
    bool  wireframe = true;     // also draw black edge lines around it?
};
} // namespace eng
