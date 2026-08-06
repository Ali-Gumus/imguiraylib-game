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

    // Fx.attachedBurst(entity, preset, ox, oy, oz [, scale]): a burst PINNED to
    // an entity, offset by ox,oy,oz measured in that entity's OWN axes - so
    // (0,0,-7) is seven metres behind its nose whichever way it is pointing.
    //
    // Use this for anything that is PART of the object rather than released by
    // it: an afterburner flame above all. Handing an ordinary burst the
    // emitter's velocity only keeps it in step while the emitter travels in a
    // straight line. An aircraft under any g is curving, so the effect gets
    // left behind toward its belly - 0.19 m at 2.2 g, 0.45 m at 3.9 g on a
    // 0.2 s effect, and the other way when pushing over. Bellyward at any roll
    // or pitch, because that is where the lift is not.
    //
    // Smoke, sparks and debris really are thrown into the air and should keep
    // using Fx.burst with a velocity: drifting is what they are supposed to do.
    fx["attachedBurst"] = [](Entity& e, const std::string& preset,
                             float ox, float oy, float oz,
                             sol::optional<float> scale) {
        BurstNamedOn(preset.c_str(), e.id, {ox, oy, oz}, scale.value_or(1.0f));
    };
}

void DescribeFxBindings(LuaApiRegistry& api) {
    api.Table("Fx").Fn("burst(preset, x, y, z [, scale [, vx, vy, vz]])",
                       "Throw a burst of particles. Presets are whatever effects.lua defines; "
                       "the velocity makes a burst inherit the motion of what made it");
    api.Table("Fx").Fn("attachedBurst(entity, preset, ox, oy, oz [, scale])",
                       "A burst PINNED to an entity, offset in its own axes. For flames and "
                       "anything else that is part of an object - a carried burst still drifts "
                       "toward the belly whenever the emitter turns");
}

} // namespace eng
