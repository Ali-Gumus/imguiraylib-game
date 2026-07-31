#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Scene.h"       // Entity

namespace eng {

void RegisterEntityBindings(sol::state& lua) {
    // Expose Entity: a script's `entity` argument can read/write these.
    lua.new_usertype<Entity>("Entity",
        "name",      &Entity::name,
        "tag",       &Entity::tag,
        "transform", &Entity::transform);
}

void DescribeEntityBindings(LuaApiRegistry& api) {
    auto e = api.Usertype("Entity", "entity");
    e.Prop("name",      "The entity's name. Unique enough to find it with Scene.find");
    e.Prop("tag",       "Its group label - \"player\", \"enemy\". What gameplay queries match on");
    e.Prop("transform", "Its Transform: position, rotation and scale");
}

} // namespace eng
