#include "engine/Audio.h"

#include "raylib.h"
#include "sol/sol.hpp"   // runs sounds.lua, where the sound definitions live

#include <algorithm>     // std::sort
#include <cstdlib>       // rand
#include <unordered_map>

namespace eng {

// ---------------------------------------------------------------------------
// One defined sound.
// ---------------------------------------------------------------------------
struct SoundDef {
    std::string name;
    std::string file;
    float volume   = 1.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    bool  loop     = false;

    // --- one-shot playback ---
    // The sound as loaded from disk, plus a pool of ALIASES. An alias shares the
    // loaded samples but has its own playback state, which is what lets several
    // copies sound at once. Without them, raylib restarts the single instance on
    // every play, so a fast-firing gun would cut itself off on every shot and be
    // heard as one continuous click.
    Sound              base{};
    std::vector<Sound> voices;
    int                nextVoice = 0;     // round-robin position
    bool               loaded    = false;

    // --- looping playback ---
    // A Music stream rather than a Sound: only Music can loop, and only Music
    // can have its pitch changed continuously while it plays, which the
    // throttle-driven engine note needs.
    Music music{};
    bool  musicLoaded  = false;
    bool  musicPlaying = false;
};

static std::unordered_map<std::string, SoundDef> s_sounds;
static std::vector<std::string> s_names;      // sorted, for the node editor
static std::vector<std::string> s_missing;    // defined but not on disk
static std::string s_error;
static bool s_ready = false;                  // did the audio device open?
static bool s_muted = false;

static float RandRange(float lo, float hi) {
    if (hi <= lo) return lo;
    float t = (float)rand() / (float)RAND_MAX;
    return lo + (hi - lo) * t;
}

// Free everything a definition owns. Aliases must be unloaded before the sound
// whose samples they share, or they would be left pointing at freed memory.
static void UnloadDef(SoundDef& d) {
    for (Sound& v : d.voices) UnloadSoundAlias(v);
    d.voices.clear();
    if (d.loaded)      { UnloadSound(d.base);        d.loaded = false; }
    if (d.musicLoaded) { UnloadMusicStream(d.music); d.musicLoaded = false; }
    d.musicPlaying = false;
}

static void UnloadAll() {
    for (auto& kv : s_sounds) UnloadDef(kv.second);
    s_sounds.clear();
}

bool InitAudio() {
    InitAudioDevice();
    // The device can fail to open - no sound card, no driver, a machine with
    // audio disabled. That must not be fatal: the game is perfectly playable
    // silently, so record it and let every other function do nothing.
    s_ready = IsAudioDeviceReady();
    if (!s_ready) {
        s_error = "audio device could not be opened; the game will be silent";
        return false;
    }
    ReloadSoundDefs();
    return true;
}

void ShutdownAudio() {
    if (!s_ready) return;
    StopAllAudio();
    UnloadAll();
    CloseAudioDevice();
    s_ready = false;
}

bool ReloadSoundDefs() {
    s_error.clear();
    s_missing.clear();
    s_names.clear();
    if (!s_ready) return false;

    // Everything is reloaded from scratch, so a sound removed from the file
    // stops existing rather than lingering from a previous load.
    UnloadAll();

    const char* path = "assets/scripts/sounds.lua";
    if (!FileExists(path)) {
        s_error = "assets/scripts/sounds.lua not found; the game will be silent";
        return false;
    }

    // A private interpreter, used only to run this one file and then discarded:
    // what it defines is kept in C++.
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math);

    sol::table snd = lua.create_named_table("sound");
    // sound.define(name, settings): register one sound. Any field left out
    // keeps a sensible default, so a definition can be a single file path.
    snd["define"] = [](const std::string& name, sol::table t) {
        SoundDef d;
        d.name = name;

        sol::optional<std::string> file = t["file"];
        d.file = file.value_or(std::string());

        auto num = [&](const char* key, float fallback) -> float {
            sol::optional<float> v = t[key];
            return v ? *v : fallback;
        };
        d.volume   = num("volume", 1.0f);
        d.pitchMin = num("pitch_min", 1.0f);
        d.pitchMax = num("pitch_max", d.pitchMin);
        int voices = (int)num("voices", 4.0f);

        sol::optional<bool> lp = t["loop"];
        d.loop = lp.value_or(false);

        // Clamp the values that would misbehave: a pitch of zero or below is
        // meaningless to the mixer, and a huge voice pool would waste memory
        // for no audible gain.
        if (d.volume   < 0.0f)  d.volume = 0.0f;
        if (d.pitchMin < 0.05f) d.pitchMin = 0.05f;
        if (d.pitchMax < d.pitchMin) d.pitchMax = d.pitchMin;
        if (voices < 1)  voices = 1;
        if (voices > 16) voices = 16;

        // A definition naming a file that is not there stays registered and
        // silent. That way the whole game can be built and played before any
        // audio has been recorded, and dropping the file in later is enough to
        // make it sound - no code change.
        if (d.file.empty() || !FileExists(d.file.c_str())) {
            if (!d.file.empty()) s_missing.push_back(name + "  (" + d.file + ")");
            s_sounds[name] = std::move(d);
            return;
        }

        if (d.loop) {
            d.music = LoadMusicStream(d.file.c_str());
            d.musicLoaded = IsMusicValid(d.music);
            if (d.musicLoaded) d.music.looping = true;
        } else {
            d.base   = LoadSound(d.file.c_str());
            d.loaded = IsSoundValid(d.base);
            if (d.loaded)
                for (int i = 0; i < voices; i++)
                    d.voices.push_back(LoadSoundAlias(d.base));
        }
        s_sounds[name] = std::move(d);
    };

    sol::protected_function_result r = lua.safe_script_file(path, sol::script_pass_on_error);
    if (!r.valid()) {
        s_error = r.get<sol::error>().what();
        return false;
    }

    for (const auto& kv : s_sounds) s_names.push_back(kv.first);
    std::sort(s_names.begin(), s_names.end());
    std::sort(s_missing.begin(), s_missing.end());
    return true;
}

void UpdateAudio() {
    if (!s_ready) return;
    // Music decodes as it plays and holds only a small buffer, so every live
    // loop has to be topped up each frame or it will stutter and fall silent.
    for (auto& kv : s_sounds) {
        SoundDef& d = kv.second;
        if (d.musicLoaded && d.musicPlaying) UpdateMusicStream(d.music);
    }
}

void PlaySoundNamed(const char* name, float volume, float pitch) {
    if (!s_ready || s_muted || name == nullptr) return;
    auto it = s_sounds.find(name);
    if (it == s_sounds.end()) return;      // unknown name: silence, not an error
    SoundDef& d = it->second;
    if (!d.loaded || d.voices.empty()) return;   // defined but its file is missing

    // Prefer a voice that is not currently sounding, so overlapping shots do not
    // cut each other off. If every voice is busy the oldest is reused, which is
    // the right compromise: the newest shot is the one the player just caused.
    int chosen = -1;
    for (int i = 0; i < (int)d.voices.size(); i++) {
        int idx = (d.nextVoice + i) % (int)d.voices.size();
        if (!IsSoundPlaying(d.voices[idx])) { chosen = idx; break; }
    }
    if (chosen < 0) chosen = d.nextVoice;
    d.nextVoice = (chosen + 1) % (int)d.voices.size();

    Sound& v = d.voices[chosen];
    SetSoundVolume(v, d.volume * volume);
    // A pitch of 0 from the caller means "use the definition's range", which
    // varies each shot. Identical repeats are what make a gun sound fake.
    SetSoundPitch(v, (pitch > 0.0f) ? pitch : RandRange(d.pitchMin, d.pitchMax));
    PlaySound(v);
}

void LoopStart(const char* name) {
    if (!s_ready || name == nullptr) return;
    auto it = s_sounds.find(name);
    if (it == s_sounds.end()) return;
    SoundDef& d = it->second;
    if (!d.musicLoaded || d.musicPlaying) return;   // already running: do nothing
    SetMusicVolume(d.music, s_muted ? 0.0f : d.volume);
    PlayMusicStream(d.music);
    d.musicPlaying = true;
}

void LoopSet(const char* name, float volume, float pitch) {
    if (!s_ready || name == nullptr) return;
    auto it = s_sounds.find(name);
    if (it == s_sounds.end()) return;
    SoundDef& d = it->second;
    if (!d.musicLoaded || !d.musicPlaying) return;
    SetMusicVolume(d.music, s_muted ? 0.0f : d.volume * volume);
    if (pitch > 0.0f) SetMusicPitch(d.music, pitch);
}

void LoopStop(const char* name) {
    if (!s_ready || name == nullptr) return;
    auto it = s_sounds.find(name);
    if (it == s_sounds.end()) return;
    SoundDef& d = it->second;
    if (d.musicLoaded && d.musicPlaying) {
        StopMusicStream(d.music);
        d.musicPlaying = false;
    }
}

void StopAllAudio() {
    if (!s_ready) return;
    for (auto& kv : s_sounds) {
        SoundDef& d = kv.second;
        for (Sound& v : d.voices)
            if (IsSoundPlaying(v)) StopSound(v);
        if (d.musicLoaded && d.musicPlaying) {
            StopMusicStream(d.music);
            d.musicPlaying = false;
        }
    }
}

int PlayingVoiceCount() {
    if (!s_ready) return 0;
    int n = 0;
    for (const auto& kv : s_sounds) {
        const SoundDef& d = kv.second;
        for (const Sound& v : d.voices)
            if (IsSoundPlaying(v)) n++;
        if (d.musicLoaded && d.musicPlaying) n++;
    }
    return n;
}

const char* SoundDefError() { return s_error.c_str(); }
const std::vector<std::string>& MissingSoundFiles() { return s_missing; }
const std::vector<std::string>& SoundNames() { return s_names; }

void SetMuted(bool muted) {
    s_muted = muted;
    if (!s_ready) return;
    // Muting has to reach the loops directly: they are already playing, so
    // unlike one-shots they will not pass through PlaySoundNamed's check.
    for (auto& kv : s_sounds) {
        SoundDef& d = kv.second;
        if (d.musicLoaded && d.musicPlaying)
            SetMusicVolume(d.music, muted ? 0.0f : d.volume);
    }
    // One-shots already sounding are left to finish; they are short by nature,
    // and cutting them dead is a harsher effect than letting them ring out.
}

bool IsMuted() { return s_muted; }

} // namespace eng
