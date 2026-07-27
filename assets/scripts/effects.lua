-- effects.lua
-- =============================================================================
-- Every visual effect in the game, described as data.
--
-- This file is read by the engine at startup and again each time you press
-- Play, so tuning an explosion means editing a number here and pressing Play --
-- no rebuild, no restart. Gameplay scripts fire these by name:
--
--     fx.burst("explosion", x, y, z)        -- at a world position
--     fx.burst("spark", x, y, z, 0.5)       -- at half size and speed
--
-- To invent a new effect, call fx.define with a new name and fire it from a
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
fx.define("explosion", {
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
fx.define("spark", {
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
fx.define("muzzle", {
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
