-- sounds.lua
-- =============================================================================
-- Every sound in the game, described as data.
--
-- The engine reads this file at startup and again each time you press Play, so
-- changing a volume means editing a number here and pressing Play -- no rebuild.
-- Gameplay scripts trigger these by name:
--
--     audio.play("shot")                  -- a one-shot sound
--     audio.play("shot", 0.5)             -- at half its defined volume
--     audio.loop_start("engine")          -- start a looping sound
--     audio.loop_set("engine", 0.6, 1.3)  -- its volume and pitch, per frame
--     audio.loop_stop("engine")
--
-- FILES GO IN assets/sounds/. None are committed to the repository, so every
-- sound below is currently SILENT: a definition whose file is missing stays
-- registered and simply makes no noise, and the editor's toolbar lists what is
-- missing. Drop a file with the matching name in and it starts working -- no
-- code change anywhere. raylib reads .wav, .ogg, .mp3 and .flac.
--
-- Every field is optional except the file:
--
--   file        path to the audio file, relative to the project root
--   volume      0..1, its own level before any per-call adjustment
--   pitch_min   lowest random pitch, where 1.0 is the file's own pitch
--   pitch_max   highest; a small spread stops repeats sounding identical
--   voices      how many copies may sound at once (one-shots only)
--   loop        true for a continuous sound whose pitch and volume change
--
-- A NOTE ON VOICES. Playing a sound that is already playing restarts it, so a
-- gun firing every 0.05 seconds would cut itself off on every shot and be heard
-- as a single stutter. Each one-shot therefore keeps a few independent copies:
-- enough that overlapping shots each get their own, no more than needed.
-- =============================================================================

-- The player's gun. Fires many times a second, so it needs several voices and a
-- little pitch variation to sound like a gun rather than a loop.
sound.define("shot", {
    file      = "assets/sounds/shot.wav",
    volume    = 0.5,
    pitch_min = 0.94,
    pitch_max = 1.08,
    voices    = 6,
})

-- A bullet striking an enemy. Short, and several can land close together.
sound.define("impact", {
    file      = "assets/sounds/impact.wav",
    volume    = 0.6,
    pitch_min = 0.9,
    pitch_max = 1.15,
    voices    = 4,
})

-- Something being destroyed. Louder, with a wider pitch spread so a wave of
-- kills does not sound mechanical.
sound.define("explosion", {
    file      = "assets/sounds/explosion.wav",
    volume    = 0.85,
    pitch_min = 0.85,
    pitch_max = 1.1,
    voices    = 4,
})

-- The player being hit. Deliberately distinct from "impact": the player needs to
-- know instantly that the damage was theirs.
sound.define("hit_taken", {
    file      = "assets/sounds/hit_taken.wav",
    volume    = 0.8,
    pitch_min = 0.95,
    pitch_max = 1.05,
    voices    = 3,
})

-- The jet's engine. A LOOP: it never stops while flying, and flight_sim.lua
-- raises its pitch and volume with the throttle. Use a sample that loops
-- seamlessly, or the seam will be audible every few seconds.
sound.define("engine", {
    file   = "assets/sounds/engine.ogg",
    volume = 0.35,
    loop   = true,
})
