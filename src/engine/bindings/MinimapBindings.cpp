#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/Minimap.h"

#include <string>

namespace eng {

void RegisterMinimapBindings(sol::state& lua) {
    lua.new_usertype<MinimapComponent>("Minimap",
        "range",       &MinimapComponent::range,
        "size",        &MinimapComponent::size,
        "corner",      &MinimapComponent::corner,
        "showTerrain", &MinimapComponent::showTerrain,
        "blipTag",     &MinimapComponent::blipTag,
        "targetTag",   &MinimapComponent::targetTag,
        "targetSize",  &MinimapComponent::targetSize
    );
    RegisterComponentAccess<MinimapComponent>(lua, "Minimap");
}

void DescribeMinimapBindings(LuaApiRegistry& api) {
    api.Usertype("Entity", "entity")
        .Method("addComponent_Minimap() -> Minimap", "Add a Minimap and return it")
        .Method("getComponent_Minimap() -> Minimap", "Its Minimap, or nil");

    auto m = api.Usertype("Minimap", "minimap");
    m.Prop("range",       "How far the radar reaches, in metres. Contacts beyond are held at the rim");
    m.Prop("size",        "Width and height on screen, in pixels");
    m.Prop("corner",      "0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right");
    m.Prop("showTerrain", "Draw the baked landscape behind the contacts");
    m.Prop("blipTag",     "Which tag counts as a contact");
    m.Prop("targetTag",   "Which tag is drawn as a target area to attack. Empty turns it off");
    m.Prop("targetSize",  "How big that target ring is, in pixels");
}

} // namespace eng
