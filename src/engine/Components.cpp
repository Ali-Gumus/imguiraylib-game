// What is left of the old single-file components unit.
// ----------------------------------------------------------------------------
// Every component moved into its own pair under components/. Two things stayed,
// because neither belongs to any one component:
//
//   * MakeComponent, the factory, which by its nature has to know all of them;
//   * the HUD value store and colour palette, which are shared state the
//     scripting API reads and writes and no component owns.

#include "engine/Components.h"

#include <string>
#include <unordered_map>

namespace eng {

static std::unordered_map<std::string, float>& HudValues() {
    static std::unordered_map<std::string, float> values;
    return values;
}
// ---- The HUD drawing palette and pass guard --------------------------------
//
// Named colours rather than raw numbers at every call site, for two reasons:
// a script reading `draw.text(s, x, y, 20, "warn")` says what it MEANS, and
// retinting the whole HUD is then one definition instead of an edit everywhere.
// draw.defineColor adds to this, so a palette can live in a script.
static std::unordered_map<std::string, Color>& HudPalette() {
    static std::unordered_map<std::string, Color> p = {
        {"hud",   Color{ 90, 255, 130, 220}},   // the established HUD green
        {"green", Color{ 90, 255, 130, 220}},
        {"warn",  Color{255, 200,  80, 230}},
        {"amber", Color{255, 200,  80, 230}},
        {"bad",   Color{255,  90,  70, 240}},
        {"red",   Color{255,  90,  70, 240}},
        {"white", Color{235, 240, 245, 235}},
        {"dim",   Color{255, 255, 255,  90}},
        {"dark",  Color{  0,   0,   0, 150}},
    };
    return p;
}

void DefineHudColor(const std::string& name, Color c) { HudPalette()[name] = c; }

Color HudColor(const std::string& name) {
    if (name.empty()) return HudPalette()["hud"];
    auto it = HudPalette().find(name);
    // An unknown name falls back to the HUD colour rather than erroring. A
    // mistyped colour should leave the element visible and slightly wrong, not
    // make it vanish - an invisible element is far harder to diagnose.
    return (it != HudPalette().end()) ? it->second : HudPalette()["hud"];
}

// True only while component HUD overlays are being drawn. The `draw.*` calls
// check this so that drawing from the wrong hook does nothing instead of
// scribbling pixel-space rectangles into the middle of the 3D pass.
static bool s_hudPass = false;
bool HudDrawAllowed() { return s_hudPass; }
void BeginHudPass()   { s_hudPass = true; }
void EndHudPass()     { s_hudPass = false; }

void SetHudValue(const std::string& key, float value) { HudValues()[key] = value; }
float GetHudValue(const std::string& key, float fallback) {
    auto it = HudValues().find(key);
    return it != HudValues().end() ? it->second : fallback;
}
void ClearHudValues() { HudValues().clear(); }


// The component factory: build a component object from its type name. Called
// while loading a scene file, which stores each component by name.
std::unique_ptr<Component> MakeComponent(const std::string& name) {
    if (name == "Shape")  return std::make_unique<ShapeComponent>();
    if (name == "Script") return std::make_unique<ScriptComponent>();
    if (name == "Graph")  return std::make_unique<GraphComponent>();
    if (name == "Camera") return std::make_unique<CameraComponent>();
    if (name == "Health") return std::make_unique<HealthComponent>();
    if (name == "Collider") return std::make_unique<ColliderComponent>();
    // "Hitbox" is the name an older scene format used for a sphere-only
    // collision volume. A Collider set to Sphere is exactly that shape, and its
    // Deserialize reads the same "radius" key, so old files keep working; the
    // next save rewrites them under the new name.
    if (name == "Hitbox") return std::make_unique<ColliderComponent>();
    if (name == "RigidBody") return std::make_unique<RigidBodyComponent>();
    if (name == "Light")  return std::make_unique<LightComponent>();
    if (name == "Model")  return std::make_unique<ModelComponent>();
    if (name == "Terrain") return std::make_unique<TerrainComponent>();
    if (name == "Minimap") return std::make_unique<MinimapComponent>();
    return nullptr;   // unknown type: caller skips it
}
} // namespace eng
