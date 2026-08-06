#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Particles.h"

#include <string>

namespace eng {

void RegisterFxBindings(sol::state& lua) {
    // The `fx` table fires visual effects. These are pure decoration: they
    // never touch the world, so a script can call one from anywhere without
    // worrying about what it might disturb.
    sol::table fx = lua.create_named_table("Fx");
    // Fx.burst(preset, x, y, z [, scale [, vx, vy, vz]]): throw a burst of
    // particles at a point. `preset` is any effect named in
    // assets/scripts/effects.lua; `scale` sizes the whole effect and defaults
    // to 1.
    //
    // vx,vy,vz are the velocity of whatever fired the effect, in world units
    // per second. Pass them when the emitter is moving: without them the burst
    // stays where it was born while the emitter flies on, so a muzzle flash
    // trails visibly behind a fast jet. A script can measure its own velocity
    // by remembering its position from the previous frame - see gun.lua.
    fx["burst"] = [](const std::string& preset, float x, float y, float z,
                     sol::optional<float> scale,
                     sol::optional<float> vx, sol::optional<float> vy,
                     sol::optional<float> vz) {
        BurstNamed(preset.c_str(), {x, y, z}, scale.value_or(1.0f),
                   {vx.value_or(0.0f), vy.value_or(0.0f), vz.value_or(0.0f)});
    };
}

void DescribeFxBindings(LuaApiRegistry& api) {
    api.Table("Fx").Fn("burst(preset, x, y, z [, scale [, vx, vy, vz]])",
                       "Throw a burst of particles. Presets are whatever effects.lua defines; "
                       "the velocity makes a burst inherit the motion of what made it");
}

} // namespace eng
