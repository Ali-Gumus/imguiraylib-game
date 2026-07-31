#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Components.h"  // every component type, for the access templates
#include "engine/Scene.h"       // Entity

namespace eng {

void RegisterEntityBindings(sol::state& lua) {
    // Expose Entity: a script's `entity` argument can read/write these.
    lua.new_usertype<Entity>("Entity",
        "name",      &Entity::name,
        "tag",       &Entity::tag,
        "transform", &Entity::transform);

    // Reaching an entity's components. One line per type, and the type list is
    // the honest statement of what a script can touch: anything not here is
    // simply not reachable, rather than reachable-but-broken.
    //
    // Camera is registered in CameraBindings instead, next to its own usertype
    // and the Projection enum, so everything about cameras is in one file.
    RegisterComponentAccess<HealthComponent>(lua, "Health");
    RegisterComponentAccess<ScriptComponent>(lua, "Script");
    RegisterComponentAccess<ShapeComponent>(lua, "Shape");
    RegisterComponentAccess<ColliderComponent>(lua, "Collider");
    RegisterComponentAccess<RigidBodyComponent>(lua, "RigidBody");
    RegisterComponentAccess<LightComponent>(lua, "Light");
    RegisterComponentAccess<ModelComponent>(lua, "Model");
    RegisterComponentAccess<TerrainComponent>(lua, "Terrain");
    RegisterComponentAccess<MinimapComponent>(lua, "Minimap");
}

void DescribeEntityBindings(LuaApiRegistry& api) {
    auto e = api.Usertype("Entity", "entity");
    e.Prop("name",      "The entity's name. Unique enough to find it with Scene.find");
    e.Prop("tag",       "Its group label - \"player\", \"enemy\". What gameplay queries match on");
    e.Prop("transform", "Its Transform: position, rotation and scale");
}

} // namespace eng
