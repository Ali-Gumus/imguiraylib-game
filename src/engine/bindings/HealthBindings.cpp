#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/Health.h"

namespace eng {

void RegisterHealthBindings(sol::state& lua) {
    lua.new_usertype<HealthComponent>("Health",
        "hp",  &HealthComponent::hp,
        "max", &HealthComponent::max
    );
    RegisterComponentAccess<HealthComponent>(lua, "Health");
}

void DescribeHealthBindings(LuaApiRegistry& api) {
    api.Usertype("Entity", "entity")
        .Method("addComponent_Health() -> Health", "Add a Health component and return it")
        .Method("getComponent_Health() -> Health", "Its Health, or nil");
    auto h = api.Usertype("Health", "health");
    h.Prop("hp",  "Current hit points. Scene.damage is the usual way to change it");
    h.Prop("max", "Starting and maximum hit points. A HUD reads this to draw a fraction");
}

} // namespace eng
