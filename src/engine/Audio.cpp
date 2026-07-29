#include "engine/Audio.h"

#include "raylib.h"
#include "raymath.h"     // vector maths for placing sounds in the world
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

    // How far away this sound can still be heard, in world units. Beyond it the
    // sound is silent and is not played at all.
    //
    // It is per sound because sounds genuinely differ: a rifle shot carries for
    // hundreds of metres while a shell casing hitting the ground does not carry
    // across a room. The default is large because this is a game of aircraft
    // over a landscape a thousand units across, where a "nearby" explosion may
    // be two hundred units away.
    float range    = 500.0f;

    // How far away the sound plays at full volume. Inside this radius it does
    // not get any louder, which stops a sound exploding towards infinite volume
    // as its source approaches the listener - the thing that makes an engine
    // note stab painfully the instant the camera passes through it.
    float refDist  = 10.0f;

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

// ---------------------------------------------------------------------------
// The listener: where the world is being heard from.
// ---------------------------------------------------------------------------
static Vector3 s_listenerPos{0.0f, 0.0f, 0.0f};
// The listener's own rightward direction. Panning is decided by which side of
// this the sound lies on, so it is stored ready-made rather than recomputed
// from forward and up on every single sound.
static Vector3 s_listenerRight{1.0f, 0.0f, 0.0f};

void SetAudioListener(Vector3 position, Vector3 forward, Vector3 up) {
    s_listenerPos = position;

    // "Right" is the direction perpendicular to both where you are looking and
    // which way is up - the cross product of the two. This is the same
    // calculation that gives a camera its sideways axis.
    Vector3 right = Vector3CrossProduct(forward, up);
    // A zero-length result means forward and up were parallel (looking straight
    // up, say), which leaves no meaningful "right". Keep the previous one
    // rather than dividing by zero and panning everything to one ear.
    if (Vector3LengthSqr(right) > 0.000001f)
        s_listenerRight = Vector3Normalize(right);
}

Vector3 GetAudioListenerPosition() { return s_listenerPos; }

// Work out how loud a sound at `pos` should be, and how far to either side.
// Returns false when the sound is out of range and should not play at all.
static bool Spatialize(const SoundDef& d, Vector3 pos, float& outGain,
                       float& outPan) {
    Vector3 toSource = Vector3Subtract(pos, s_listenerPos);
    float   dist     = Vector3Length(toSource);

    if (dist >= d.range) return false;          // too far to hear

    // --- Volume ---
    // Full volume inside refDist, then falling to nothing at range. The falloff
    // is SQUARED rather than straight-line: sound intensity in the real world
    // drops with the square of distance, and a linear fade sounds wrong in a
    // specific way - things stay too loud too long and then vanish abruptly.
    if (dist <= d.refDist) {
        outGain = 1.0f;
    } else {
        float t = (dist - d.refDist) / (d.range - d.refDist);   // 0 near, 1 far
        float fade = 1.0f - t;
        outGain = fade * fade;
    }

    // --- Side ---
    // How far the source lies along the listener's rightward axis, as a
    // fraction of its distance: +1 is directly to the right, -1 directly to the
    // left, 0 straight ahead OR straight behind (stereo cannot tell those
    // apart).
    float side = (dist > 0.0001f)
               ? Vector3DotProduct(Vector3Scale(toSource, 1.0f / dist),
                                   s_listenerRight)
               : 0.0f;

    // A sound very close to the listener should not be hard in one ear - when
    // something is almost on top of you it surrounds you. Fading the panning
    // out at close range avoids a sound snapping from one side to the other as
    // the source passes through the camera.
    float closeness = (dist < d.refDist) ? (dist / d.refDist) : 1.0f;
    side *= closeness;

    // raylib's pan runs from -1 (hard left) through 0 (centred) to +1 (hard
    // right), which is exactly the range `side` already covers - so it goes
    // straight across.
    //
    // Worth checking rather than assuming, because raylib's own header comments
    // are inconsistent about this and a 0..1 reading would be wrong in a way
    // that is easy to miss: centre would sit half-right, and nothing would ever
    // be heard on the left at all. The authority is SetAudioBufferPan, which
    // clamps to [-1, 1], and the mixer's `right = (pan + 1)/2`.
    outPan = side;
    return true;
}

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
        d.range    = num("range", 500.0f);
        d.refDist  = num("ref_dist", 10.0f);
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

// The shared body of both play functions. `gain` already includes any distance
// attenuation, and `pan` is raylib's -1 (left) to +1 (right); 0 is centred,
// which is what an unpositioned sound uses.
static void PlayOneShot(SoundDef& d, float volume, float pitch, float pan) {
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
    SetSoundPan(v, pan);
    PlaySound(v);
}

void PlaySoundNamed(const char* name, float volume, float pitch) {
    if (!s_ready || s_muted || name == nullptr) return;
    auto it = s_sounds.find(name);
    if (it == s_sounds.end()) return;      // unknown name: silence, not an error
    SoundDef& d = it->second;
    if (!d.loaded || d.voices.empty()) return;   // defined but its file is missing

    // No position, so no attenuation and dead centre.
    PlayOneShot(d, volume, pitch, 0.0f);
}

void PlaySoundNamedAt(const char* name, Vector3 position, float volume,
                      float pitch) {
    if (!s_ready || s_muted || name == nullptr) return;
    auto it = s_sounds.find(name);
    if (it == s_sounds.end()) return;
    SoundDef& d = it->second;
    if (!d.loaded || d.voices.empty()) return;

    float gain = 1.0f, pan = 0.0f;
    // Out of earshot: do not play it at all. Beyond being pointless, a silent
    // distant shot would still claim a voice from the pool, and the pool is
    // what lets nearby shots overlap instead of cutting each other off.
    if (!Spatialize(d, position, gain, pan)) return;

    PlayOneShot(d, volume * gain, pitch, pan);
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

void LoopSetAt(const char* name, Vector3 position, float volume, float pitch) {
    if (!s_ready || name == nullptr) return;
    auto it = s_sounds.find(name);
    if (it == s_sounds.end()) return;
    SoundDef& d = it->second;
    if (!d.musicLoaded || !d.musicPlaying) return;

    float gain = 0.0f, pan = 0.0f;
    // Unlike a one-shot, an out-of-range LOOP is not stopped - it is turned
    // down to silence and left running. A helicopter that flies away and comes
    // back should fade out and in; stopping the stream would restart it from
    // the beginning of the sample, which is audible as a click and a jump.
    if (!Spatialize(d, position, gain, pan)) gain = 0.0f;

    SetMusicVolume(d.music, s_muted ? 0.0f : d.volume * volume * gain);
    SetMusicPan(d.music, pan);
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
