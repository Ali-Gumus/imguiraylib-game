#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/Collider.h"

namespace eng {

void RegisterColliderBindings(sol::state& lua) {
    lua.new_enum<ColliderShape>("ColliderShape", {
        {"Sphere",      ColliderShape::Sphere},
        {"Box",         ColliderShape::Box},
        {"Capsule",     ColliderShape::Capsule},
        {"Heightfield", ColliderShape::Heightfield},
    });

    lua.new_usertype<ColliderComponent>("Collider",
        "shape",       &ColliderComponent::shape,
        "radius",      &ColliderComponent::radius,
        "height",      &ColliderComponent::height,
        "halfExtents", &ColliderComponent::halfExtents,
        "offset",      &ColliderComponent::offset,
        "rotation",    &ColliderComponent::rotation
    );
    RegisterComponentAccess<ColliderComponent>(lua, "Collider");
}

void DescribeColliderBindings(LuaApiRegistry& api) {
    api.Usertype("Entity", "entity")
        .Method("addComponent_Collider() -> Collider", "Add a Collider and return it")
        .Method("getComponent_Collider() -> Collider", "Its Collider, or nil");

    auto c = api.Usertype("Collider", "collider");
    c.Prop("shape",       "ColliderShape.Sphere, .Box, .Capsule or .Heightfield");
    c.Prop("radius",      "Sphere and capsule radius. In WORLD metres - a collider ignores entity scale");
    c.Prop("height",      "The straight middle section of a capsule, ends not included");
    c.Prop("halfExtents", "Half the width, height and depth of a box");
    c.Prop("offset",      "Where the shape sits inside the entity");
    c.Prop("rotation",    "Euler degrees. A capsule is built along its own Y, so 90 on X lays it nose to tail");

    api.Table("ColliderShape")
        .Value("Sphere",      "A ball. Cheapest to test")
        .Value("Box",         "A rectangular block that turns with the entity")
        .Value("Capsule",     "A cylinder with rounded ends. The usual choice for anything long")
        .Value("Heightfield", "The landscape, taken from the Terrain on the same entity");
}

} // namespace eng
