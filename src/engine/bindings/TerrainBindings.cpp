#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/Terrain.h"

namespace eng {

void RegisterTerrainBindings(sol::state& lua) {
    lua.new_usertype<TerrainComponent>("Terrain",
        "worldSize",  &TerrainComponent::worldSize,
        "maxHeight",  &TerrainComponent::maxHeight,
        "resolution", &TerrainComponent::resolution,
        // Exposed as "hillScale" rather than the field's own noiseScale,
        // because that is what the Inspector calls it and a script and a panel
        // disagreeing about the name of the same number helps nobody.
        "hillScale",  &TerrainComponent::noiseScale,
        "seed",       &TerrainComponent::seed,
        "tint",       &TerrainComponent::tint,
        "wire",       &TerrainComponent::wire,
        // Nothing above takes effect until the mesh is rebuilt. That is
        // deliberate: regenerating a quarter of a million triangles on every
        // assignment would make editing terrain from a script unusable.
        "rebuild",    &TerrainComponent::Rebuild
    );
    RegisterComponentAccess<TerrainComponent>(lua, "Terrain");
}

void DescribeTerrainBindings(LuaApiRegistry& api) {
    api.Usertype("Entity", "entity")
        .Method("addComponent_Terrain() -> Terrain", "Add a Terrain and return it")
        .Method("getComponent_Terrain() -> Terrain", "Its Terrain, or nil");

    auto t = api.Usertype("Terrain", "terrain");
    t.Prop("worldSize",  "How many metres across the landscape is");
    t.Prop("maxHeight",  "Height of the tallest ground");
    t.Prop("resolution", "Grid detail. A power of two up to 512 keeps the collision surface exact");
    t.Prop("hillScale",  "How many ridges span the map. Low numbers give big ranges, not fewer bumps");
    t.Prop("seed",       "Change for a different landscape");
    t.Prop("tint",       "Ground colour");
    t.Prop("wire",       "Overlay contour lines");
    t.Method("rebuild()", "Regenerate the mesh. Nothing else here takes effect until you call it");
}

} // namespace eng
