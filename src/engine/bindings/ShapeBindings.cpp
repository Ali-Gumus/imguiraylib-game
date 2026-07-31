#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/Shape.h"

namespace eng {

void RegisterShapeBindings(sol::state& lua) {
    // Named "Primitive" rather than "Shape". ColliderShape already speaks about
    // shapes, and the usertype below takes the name Shape - two different things
    // called Shape in one state is a trap where the wrong one silently answers.
    lua.new_enum<ShapeComponent::Kind>("Primitive", {
        {"Cube",     ShapeComponent::Kind::Cube},
        {"Sphere",   ShapeComponent::Kind::Sphere},
        {"Cylinder", ShapeComponent::Kind::Cylinder},
        {"Cone",     ShapeComponent::Kind::Cone},
        {"Plane",    ShapeComponent::Kind::Plane},
    });

    lua.new_usertype<ShapeComponent>("Shape",
        "kind",      &ShapeComponent::kind,
        "tint",      &ShapeComponent::tint,
        "wireframe", &ShapeComponent::wireframe
    );
    RegisterComponentAccess<ShapeComponent>(lua, "Shape");
}

void DescribeShapeBindings(LuaApiRegistry& api) {
    api.Usertype("Entity", "entity")
        .Method("addComponent_Shape() -> Shape", "Add a primitive Shape and return it")
        .Method("getComponent_Shape() -> Shape", "Its Shape, or nil");

    auto s = api.Usertype("Shape", "shape");
    s.Prop("kind",      "Primitive.Cube, .Sphere, .Cylinder, .Cone or .Plane");
    s.Prop("tint",      "Its colour");
    s.Prop("wireframe", "Draw an outline over it as well");

    api.Table("Primitive")
        .Value("Cube",     "A box")
        .Value("Sphere",   "A ball")
        .Value("Cylinder", "A tube")
        .Value("Cone",     "A cone")
        .Value("Plane",    "A flat quad");
}

} // namespace eng
