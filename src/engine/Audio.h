#pragma once

// ============================================================================
// Audio: the game's sound effects and its looping engine note.
// ----------------------------------------------------------------------------
// Like the particle system, the machinery lives in C++ and the DATA lives in
// Lua: assets/scripts/sounds.lua names every sound and its settings, and
// gameplay scripts trigger them by name:
//
//     audio.play("shot")
//     audio.loop_start("engine");  audio.loop_set("engine", 0.5, 1.2)
//
// Two kinds of sound exist, because they are genuinely different things:
//
//   * A ONE-SHOT (a gunshot, an impact, an explosion) is loaded whole into
//     memory and fired off. Several may overlap.
//   * A LOOP (the engine note) plays continuously and has its volume and pitch
//     changed while it runs. raylib's Sound cannot loop, so these are loaded as
//     a Music stream, which can - at the cost of needing to be topped up every
//     frame (see UpdateAudio).
//
// Everything degrades quietly. If the audio device will not open, or a named
// file is missing, the calls still work and simply make no sound; the missing
// files are listed so the silence can be explained instead of guessed at.
// ============================================================================

#include "raylib.h"   // Vector3, for the listener and for positioned sounds

#include <string>
#include <vector>

namespace eng {

// --- Positional sound -------------------------------------------------------
// A sound played with a POSITION is heard from where it happened: quieter with
// distance, and further to one ear than the other depending on which side of
// you it is on. Without this every sound arrives at full volume dead centre,
// as though it were happening inside your head - a distant explosion is as
// loud as your own gun, and nothing tells you where anything is.
//
// Two effects are combined, and they answer different questions:
//   * ATTENUATION - "how far away?" Volume falls off with distance, reaching
//     silence at the sound's `range` (set per sound in sounds.lua).
//   * PANNING - "which side?" The sound is weighted towards the ear it is on,
//     found by comparing the direction of the source with the listener's own
//     rightward direction.
//
// This is stereo panning, not true 3D audio: it cannot distinguish directly in
// front from directly behind, since both are equally between the two ears. That
// is the normal limitation of stereo and is rarely noticed in a game with a
// camera, because the picture answers the question the sound cannot.

// Tell the audio system where the listener is and which way it faces. Call once
// per frame, BEFORE any sound is played, or positioned sounds will be judged
// against the previous frame's listener.
//
// `forward` and `up` describe the orientation; the rightward direction used for
// panning is derived from them. Passing the active camera's position and
// orientation is what makes what you hear match what you see.
void SetAudioListener(Vector3 position, Vector3 forward, Vector3 up);

// Where the listener currently is, so callers can measure against it.
Vector3 GetAudioListenerPosition();

// Open the audio device and read the sound definitions. Call ONCE at startup.
// Returns false if the device could not be opened, after which every function
// here is a harmless no-op.
bool InitAudio();

// Stop everything, free the loaded sounds and close the device. Call at exit.
void ShutdownAudio();

// Keep looping sounds fed. Music streams hold only a few seconds of decoded
// audio at a time and must be refilled continuously, so this has to run every
// frame or a loop will stutter and stop.
void UpdateAudio();

// Play a one-shot sound by name.
//   volume - multiplies the volume set in the data file (1 = as defined).
//   pitch  - 0 means "pick from the definition's random range", which keeps
//            repeated shots from sounding like one stuttering sample. Any other
//            value plays at exactly that pitch (1 = the file's own pitch).
// An unknown name does nothing at all: a typo must not stop the game, and a
// silent effect is a far better failure than an exception mid-firefight.
void PlaySoundNamed(const char* name, float volume = 1.0f, float pitch = 0.0f);

// The same, but heard from a point in the world: quieter with distance and
// weighted towards the ear it happened on. Use this for anything that happens
// somewhere - a gun firing, a shell landing, a jet exploding. Reserve the
// unpositioned version above for sounds that genuinely have no place in the
// world, such as a menu click or a warning tone meant for the player rather
// than the pilot.
//
// A sound past its definition's `range` is not played at all, which is both
// correct and useful: distant gunfire does not quietly steal a voice from the
// pool that a nearby shot needs.
void PlaySoundNamedAt(const char* name, Vector3 position,
                      float volume = 1.0f, float pitch = 0.0f);

// Looping sounds. Starting one that is already running does nothing, so a
// script can call LoopStart every frame without stacking up copies.
void LoopStart(const char* name);
void LoopSet(const char* name, float volume, float pitch);
void LoopStop(const char* name);

// A looping sound coming from a moving point - a helicopter passing overhead.
// Call it every frame with the source's current position, exactly as LoopSet is
// called every frame with the current throttle: the distance and the side both
// change as the thing moves, and a loop placed once would stay where it started.
void LoopSetAt(const char* name, Vector3 position, float volume, float pitch);

// Silence everything immediately. Used when play mode starts and stops: a
// looping engine note must not outlive the run that started it, the same rule
// the HUD store and the particle pool follow.
void StopAllAudio();

// Re-read assets/scripts/sounds.lua. Called at startup and on each Play, so
// retuning a volume means editing that file and pressing Play - no rebuild.
bool ReloadSoundDefs();

// The error from the last load, or "" if it succeeded.
const char* SoundDefError();

// Sounds that are defined but whose file is not on disk. These are the ones
// that will be silent, and the editor lists them so that silence is explained.
const std::vector<std::string>& MissingSoundFiles();

// Every defined sound name, sorted. The node editor lists these in a dropdown.
const std::vector<std::string>& SoundNames();

// How many sounds are actually sounding right now, counting each overlapping
// voice and each running loop. Shown in the editor's statistics readout, which
// is what turns "I heard nothing" into a question with an answer: nothing
// playing, or playing but inaudible.
int PlayingVoiceCount();

// Global mute. The editor runs dozens of times an hour and a jet engine every
// time is intolerable, so this is a genuine convenience rather than polish.
void SetMuted(bool muted);
bool IsMuted();

} // namespace eng
