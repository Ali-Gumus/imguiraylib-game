-- sounds.lua
-- =============================================================================
-- Every sound in the game, described as data.
--
-- The engine reads this file at startup and again each time you press Play, so
-- changing a volume means editing a number here and pressing Play -- no rebuild.
-- Gameplay scripts trigger these by name:
--
--     audio.play("shot")               -- a one-shot sound
--     audio.play("shot", 0.5)          -- at half its defined volume
--     audio.loop_start("jet")          -- start a looping sound
--     audio.loop_set("jet", 0.6, 1.3)  -- its volume and pitch, per frame
--     audio.loop_stop("jet")
--
-- ...or, for anything that happens SOMEWHERE in the world:
--
--     audio.play_at("impact", x, y, z)          -- heard from that point
--     audio.loop_at("jet", x, y, z, 0.6, 1.3)   -- a loop that moves with it
--
-- A positioned sound gets quieter with distance and leans towards the ear it
-- is on. Prefer it for everything that physically happens - guns, impacts,
-- explosions, engines - and keep the plain audio.play for sounds aimed at the
-- PLAYER rather than the pilot, such as a warning tone or the cue that you
-- have been hit, which must never be faint or off to one side.
--
-- FILES GO IN assets/sounds/. A definition whose file is missing stays
-- registered and simply makes no noise, and the editor's toolbar shows a "snd?"
-- badge listing which ones those are - so silence is always explained. Drop a
-- file in and it starts working with no code change anywhere. raylib reads
-- .wav, .ogg, .mp3 and .flac.
--
-- A LOOP IS ONE STREAM, SHARED BY NAME. Two entities that both call
-- loop_start("heli") get the same single stream, not one each. It can now be
-- given a position with loop_at, but there is still only ONE of it: with two
-- helicopters, whichever calls loop_at last that frame decides where the sound
-- appears to come from. That is fine for the player's own engine note, and for
-- a single enemy; several at once would need a stream per source.
--
-- Every field is optional except the file:
--
--   file        path to the audio file, relative to the project root
--   volume      0..1, its own level before any per-call adjustment
--   pitch_min   lowest random pitch, where 1.0 is the file's own pitch
--   pitch_max   highest; a small spread stops repeats sounding identical
--   voices      how many copies may sound at once (one-shots only)
--   loop        true for a continuous sound whose pitch and volume change
--   range       how far away it can still be heard, in world units. Past this
--               a positioned sound is silent and is not played at all. Set it
--               by what the sound IS: gunfire and explosions carry a long way,
--               a bullet striking dirt does not. Ignored by audio.play.
--   ref_dist    how close it can get before it stops growing louder. Without
--               it, a sound rushes towards deafening as its source reaches the
--               camera; it also fades the left-right split near the listener,
--               so a source passing through the camera does not snap from one
--               ear to the other.
--
-- A NOTE ON VOICES. Playing a sound that is already playing restarts it, so a
-- gun firing every 0.05 seconds would cut itself off on every shot and be heard
-- as a single stutter. Each one-shot therefore keeps a few independent copies:
-- enough that overlapping shots each get their own, no more than needed.
-- =============================================================================

-- The player's gun. Fires many times a second, so it needs several voices and a
-- little pitch variation to sound like a gun rather than a loop.
sound.define("shot", {
    file      = "assets/sounds/heavy-machine-gun-50-caliber.mp3",
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
    file      = "assets/sounds/explosion.mp3",
    volume    = 3,
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
sound.define("jet", {
    file   = "assets/sounds/jet-loop-01.mp3",
    volume = 0.35,
    loop   = true,
})

sound.define("heli", {
    file   = "assets/sounds/helicopter-blades.mp3",
    volume = 0.35,
    loop   = true,
})
