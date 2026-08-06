-- effects.lua
-- =============================================================================
-- Every visual effect in the game, described as data.
--
-- This file is read by the engine at startup and again each time you press
-- Play, so tuning an explosion means editing a number here and pressing Play --
-- no rebuild, no restart. Gameplay scripts fire these by name:
--
--     Fx.burst("explosion", x, y, z)        -- at a world position
--     Fx.burst("spark", x, y, z, 0.5)       -- at half size and speed
--
-- To invent a new effect, call Fx.define with a new name and fire it from a
-- script by that name. Nothing in C++ needs to change.
--
-- Every field is optional. Anything you leave out keeps the value the effect
-- already had, so you can override one number without restating the rest.
--
--   count        how many particles per burst (0..512)
--   speed_min    slowest a particle flies outward, world units per second
--   speed_max    fastest
--   life_min     shortest a particle lives, in seconds
--   life_max     longest
--   size_start   width in world units when it is born
--   size_end     width when it dies (smaller reads as burning out)
--   color_start  {red, green, blue, alpha}, each 0..255, at birth
--   color_end    the same, at death; end with alpha 0 so it fades out
--   gravity      downward acceleration; negative falls, 0 floats
--   drag         how fast it loses speed; higher stops sooner
--   up_bias      pushes the spread upward, as hot gas would rise
--
-- A NOTE ON COLOUR. Particles are drawn ADDITIVELY: each one adds its colour to
-- what is already on screen rather than covering it. That is why fire looks
-- right -- overlapping particles brighten towards white, exactly as flames do --
-- but it has two consequences worth knowing:
--   * Starting near white makes a dense burst saturate into a flat white blob.
--     Start further down the scale and let the overlap do the brightening.
--   * Dark colours are invisible, because adding a dark colour changes almost
--     nothing. Smoke cannot be made this way.
-- =============================================================================

-- Something died: a fireball that throws embers outward and burns down to a
-- dark red as it falls.
Fx.define("explosion", {
    count       = 60,
    speed_min   = 4.0,
    speed_max   = 26.0,
    life_min    = 0.5,
    life_max    = 1.4,
    size_start  = 1.1,
    size_end    = 0.12,
    color_start = {255, 185, 90, 215},
    color_end   = {120, 30, 10, 0},
    gravity     = -9.0,
    drag        = 2.2,
    up_bias     = 0.35,
})

-- A bullet connected: a few fast white-hot flecks, gone almost at once. They
-- are over before gravity has time to matter.
Fx.define("spark", {
    count       = 14,
    speed_min   = 6.0,
    speed_max   = 20.0,
    life_min    = 0.12,
    life_max    = 0.35,
    size_start  = 0.35,
    size_end    = 0.05,
    color_start = {255, 250, 220, 255},
    color_end   = {255, 130, 40, 0},
    gravity     = -4.0,
    drag        = 3.0,
    up_bias     = 0.0,
})

-- A gun fired. This one has to be generous to be seen at all: it is drawn
-- against a bright sky, and at sixty frames a second an effect lasting a tenth
-- of a second is only six frames on screen. Too few, too small or too brief and
-- it disappears entirely.
Fx.define("muzzle", {
    count       = 22,
    speed_min   = 1.0,
    speed_max   = 8.0,
    life_min    = 0.10,
    life_max    = 0.22,
    size_start  = 1.5,
    size_end    = 0.25,
    color_start = {255, 240, 190, 255},
    color_end   = {255, 120, 30, 0},
    gravity     = 0.0,
    drag        = 5.0,
    up_bias     = 0.0,
})

-- =============================================================================
-- THE ENGINE. Two effects, emitted CONTINUOUSLY rather than as a one-off burst,
-- so both are deliberately tiny: a handful of particles per frame at sixty
-- frames a second is already a steady cloud, and the same numbers that make a
-- good explosion would fill the 2048-particle budget in a couple of seconds.
--
-- Both are emitted carrying the aircraft's own velocity, so they sit at the
-- tailpipe instead of being left behind - see flight_jsb.lua. A trail that
-- hangs in the air would need the opposite, and a much longer life.
-- =============================================================================

-- Dry thrust: the shimmer of hot gas leaving the nozzle. Barely there on
-- purpose. This is the one that says the engine is RUNNING, and an exhaust you
-- notice is an exhaust that has stopped reading as exhaust.
--
-- A faint blue-grey rather than orange, because dry exhaust is not burning -
-- it is just hot. Remember dark colours cannot be drawn additively at all, so
-- "faint" here has to mean a dim colour with low alpha, not a dark one.
Fx.define("jet_exhaust", {
    count       = 5,
    speed_min   = 0.5,
    speed_max   = 3.0,
    life_min    = 0.06,
    life_max    = 0.14,
    size_start  = 1.6,
    size_end    = 2.0,      -- widens as it disperses, unlike a spark
    color_start = {255, 100, 100, 120},
    color_end   = {60, 70, 90, 0},
    gravity     = 0.0,
    drag        = 3.0,
    up_bias     = 0.0,
})

-- Reheat: raw fuel burning in the exhaust, which is what makes the flame you
-- can see from miles away. Bright, and started well below white so that the
-- overlap of several does the brightening rather than saturating flat.
--
-- The colour runs from a pale blue-white core out to orange, which is the way
-- round a real afterburner goes - hottest and bluest at the nozzle, cooling to
-- orange down the plume. size_end is larger than size_start for the same
-- reason the plume spreads.
Fx.define("jet_burner", {
    count       = 1,
    speed_min   = 1.0,
    speed_max   = 6.0,
    life_min    = 0.08,
    life_max    = 0.15,
    size_start  = 1.6,
    size_end    = 2.0,
    color_start = {252, 100, 100, 200},
    color_end   = {255, 110, 20, 0},
    gravity     = 0.0,
    drag        = 2.0,
    up_bias     = 0.0,
})
