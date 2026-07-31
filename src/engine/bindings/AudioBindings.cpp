#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Audio.h"

#include <string>

namespace eng {

void RegisterAudioBindings(sol::state& lua) {
    // The `audio` table plays sounds. Like fx, these are pure output: they never
    // touch the world, so they are safe to call from anywhere.
    sol::table au = lua.create_named_table("Audio");
    // Audio.play(name [, volume [, pitch]]): fire a one-shot sound. The names
    // come from assets/scripts/sounds.lua. `volume` scales the level set there;
    // `pitch` defaults to the definition's random range, which is what keeps
    // repeated gunfire from sounding like one stuttering sample.
    au["play"] = [](const std::string& name, sol::optional<float> volume,
                    sol::optional<float> pitch) {
        PlaySoundNamed(name.c_str(), volume.value_or(1.0f), pitch.value_or(0.0f));
    };
    // Looping sounds, for anything continuous like an engine note. Starting one
    // that is already running does nothing, so a script may call loopStart
    // every frame without stacking up copies.
    // Audio.playAt(name, x, y, z [, volume [, pitch]]): the same one-shot, but
    // heard FROM A PLACE - quieter with distance and weighted towards the ear
    // it happened on. Use this for anything that happens somewhere in the world
    // (a gun, an impact, an explosion) and keep Audio.play for sounds that
    // genuinely have no location, like a warning tone meant for the player
    // rather than the pilot. Beyond the sound's range it is not played at all.
    au["playAt"] = [](const std::string& name, float x, float y, float z,
                       sol::optional<float> volume, sol::optional<float> pitch) {
        PlaySoundNamedAt(name.c_str(), {x, y, z}, volume.value_or(1.0f),
                         pitch.value_or(0.0f));
    };

    au["loopStart"] = [](const std::string& name) { LoopStart(name.c_str()); };
    au["loopStop"]  = [](const std::string& name) { LoopStop(name.c_str()); };
    // Audio.loopSet(name, volume, pitch): change a running loop. Called every
    // frame to tie an engine note to the throttle.
    au["loopSet"] = [](const std::string& name, float volume, float pitch) {
        LoopSet(name.c_str(), volume, pitch);
    };
    // Audio.loopAt(name, x, y, z, volume, pitch): a loop coming from a moving
    // point, such as a helicopter passing overhead. Call it every frame with
    // the source's current position - both the distance and the side change as
    // the thing moves, and a loop positioned once would stay where it started.
    // Out of range it fades to silence rather than stopping, so a source that
    // flies away and returns fades back in instead of restarting with a click.
    au["loopAt"] = [](const std::string& name, float x, float y, float z,
                       float volume, float pitch) {
        LoopSetAt(name.c_str(), {x, y, z}, volume, pitch);
    };
}

void DescribeAudioBindings(LuaApiRegistry& api) {
    auto a = api.Table("Audio");
    a.Fn("play(name [, volume [, pitch]])",
         "Fire a one-shot sound. Names come from sounds.lua");
    a.Fn("playAt(name, x, y, z)",
         "The same, heard from a point in the world: quieter with distance and on the ear it is nearer");
    a.Fn("loopStart(name)", "Start a looping sound");
    a.Fn("loopSet(name, volume, pitch)",
         "Adjust a running loop - what makes an engine note follow the throttle");
    a.Fn("loopAt(name, x, y, z [, volume [, pitch]])",
         "Position a running loop in the world. ONE stream per name, so two sources share it");
    a.Fn("loopStop(name)", "Stop a looping sound");
}

} // namespace eng
