#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Components.h"  // SetHudValue / GetHudValue

#include <string>

namespace eng {

void RegisterHudBindings(sol::state& lua) {
    // Hud.set(name, value): publish a number for the editor's HUD to display
    // (e.g. Hud.set("throttle", throttle) from the flight script).
    sol::table hud = lua.create_named_table("Hud");
    hud["set"] = [](const std::string& k, float v) { SetHudValue(k, v); };
    // Hud.get(name[, fallback]): read a published value back (0 if never set).
    // This makes the HUD store double as shared game state a script can read.
    hud["get"] = [](const std::string& k, sol::optional<float> fb) {
        return GetHudValue(k, fb.value_or(0.0f));
    };
    // Hud.add(name, delta): add to a value (e.g. Hud.add("score", 1)).
    hud["add"] = [](const std::string& k, float d) {
        SetHudValue(k, GetHudValue(k, 0.0f) + d);
    };
}

void DescribeHudBindings(LuaApiRegistry& api) {
    auto h = api.Table("Hud");
    h.Fn("set(name, value)", "Publish a number under a name");
    h.Fn("get(name [, fallback]) -> number",
         "Read one back. The fallback is what an element uses to hide itself when nothing published");
    h.Fn("add(name, delta)", "Add to a value - awarding score, counting waves");
}

} // namespace eng
