#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Components.h"
#include "engine/Lighting.h"
#include "engine/Scene.h"

namespace eng {

void RegisterLightBindings(sol::state& lua) {
    // The `light` table lets scripts change the sun while the game runs: dimming
    // it towards dusk, flashing it red when the player is hit, and so on.
    //
    // Every one of these writes into the scene's first Light COMPONENT rather
    // than into the shader. That is deliberate. The editor pushes the light
    // component to the shader at the start of every frame, so a value written
    // straight to the shader would be wiped before anything was drawn with it.
    // Writing to the component also means the Inspector keeps showing the truth.
    //
    // The sun's DIRECTION has no function here because it needs none: the light
    // travels along its entity's forward axis, so a script aims it by rotating
    // that entity like any other object.
    sol::table lt = lua.create_named_table("light");

    // Find the scene's light, or nullptr when the scene has none. Every
    // function below goes through this, so a scene without a light simply does
    // nothing instead of failing.
    auto findLight = []() -> LightComponent* {
        Scene* s = Scene::Current();
        if (!s) return nullptr;
        for (Entity& e : s->Entities())
            if (auto* l = e.GetComponent<LightComponent>()) return l;
        return nullptr;
    };

    // light.set_color(r, g, b): channels are 0..255, matching the numbers the
    // Inspector's colour picker shows, and are converted to the 0..1 the shader
    // works in.
    lt["set_color"] = [findLight](float r, float g, float b) {
        if (auto* l = findLight())
            l->color = {r / 255.0f, g / 255.0f, b / 255.0f};
    };
    // light.set_intensity(v): brightness, separate from colour. 1 is normal,
    // 0 is night, above 1 is brighter than the surface's own colour.
    lt["set_intensity"] = [findLight](float v) {
        if (auto* l = findLight()) l->intensity = v;
    };
    // light.set_ambient(r, g, b): the light reaching surfaces the sun cannot
    // see. Lower it for harsh, high-contrast shadows; raise it for a flat,
    // overcast look.
    lt["set_ambient"] = [findLight](float r, float g, float b) {
        if (auto* l = findLight())
            l->ambient = {r / 255.0f, g / 255.0f, b / 255.0f};
    };
    // light.get_intensity(): read it back, so a script can fade from wherever
    // the light currently is rather than from a number it assumed.
    lt["get_intensity"] = [findLight]() -> float {
        auto* l = findLight();
        return l ? l->intensity : 0.0f;
    };
}

void DescribeLightBindings(LuaApiRegistry& api) {
    auto l = api.Table("light");
    l.Fn("set_intensity(value)", "Set the sun's brightness. Written to the component, not the shader");
    l.Fn("get_intensity() -> number", "Read it back");
    l.Fn("set_color(r, g, b)",
         "Set the sun's colour, channels 0..1. Warms or cools everything the sun touches");
    l.Fn("set_ambient(value)",
         "Set the fill light that reaches surfaces facing away from the sun. "
         "At zero the shadowed side of everything is pure black");
}

} // namespace eng
