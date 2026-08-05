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
        "transform", &Entity::transform,

        // How fast it is moving through the world, metres per second.
        //
        // READ-ONLY, because assigning a velocity here would not move anything -
        // this is a measurement, not a control. With safeties on, trying raises
        // an error naming it rather than silently doing nothing.
        //
        // Ask for this rather than working it out from successive positions.
        // The engine samples every entity at one fixed point in the frame, so
        // the answer is right whatever order a script's component sits in;
        // a script measuring for itself divides one frame's displacement by
        // another frame's dt and is wrong in proportion to how much the frame
        // rate wobbled. See Entity::velocity in Scene.h.
        "velocity", sol::readonly_property(
            [](Entity& e) { return e.velocity; }));

    // Component access is registered by each component's OWN binding file, next
    // to the usertype it hands back. The two are useless apart: an accessor
    // that returns a type Lua was never told about gives back opaque userdata
    // with no readable fields, which looks like it works until you touch it.
    //
    // Light is the exception and stays here, because its component's settings
    // are reached through the `Light` table rather than through the component -
    // there is only ever one sun, so there is nothing to look up per entity.
    RegisterComponentAccess<LightComponent>(lua, "Light");
}

void DescribeEntityBindings(LuaApiRegistry& api) {
    auto e = api.Usertype("Entity", "entity");
    e.Prop("name",      "The entity's name. Unique enough to find it with Scene.find");
    e.Prop("tag",       "Its group label - \"player\", \"enemy\". What gameplay queries match on");
    e.Prop("transform", "Its Transform: position, rotation and scale");
    e.Prop("velocity",  "How fast it moves through the world, m/s, as a Vector3. "
                        "READ-ONLY, and measured by the engine at one fixed point "
                        "in the frame - use this rather than differencing positions");
}

} // namespace eng
