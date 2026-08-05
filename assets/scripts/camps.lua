-- camps.lua
-- =============================================================================
-- Lays out ENEMY CAMPS across the landscape and keeps score of them. Each camp
-- is a headquarters, a few outbuildings, and anti-aircraft vehicles defending
-- all of it. Flattening the HQ is what finishes a camp.
--
-- Put this on the Game Manager, or on any entity that lives from the start. It
-- builds the world in onStart and then only reports how many camps are left.
--
-- WHY CAMPS RATHER THAN SCATTERED GUNS. Guns spread evenly over a 40 km map are
-- a constant low chance of being shot at wherever you fly, which is weather
-- rather than a threat, and nothing to aim at. A camp is a PLACE: something
-- worth flying to, defences arranged around it, and a reason to choose an
-- approach. The guns are sited between the buildings and the sky rather than on
-- top of them, so the last part of an attack run is the dangerous part.
--
-- HOW ANYTHING ENDS UP ON THE GROUND. Scene.groundHeight(x, z) gives the terrain
-- surface at a point, so every building and vehicle is placed at the height the
-- ground actually is there. Without it the whole camp would be buried in a
-- hillside or hanging over a valley, and which one would depend on the seed.
-- =============================================================================

properties = {
    -- How many camps to build.
    camp_count = 4,

    -- The defences and the buildings of one camp.
    guns_per_camp = 4,
    huts_per_camp = 4,

    -- How far the outbuildings and the guns sit from the HQ, in metres. The
    -- guns are further out than the buildings on purpose: they have to be met
    -- BEFORE the target, or they would only fire once the bombs were already
    -- away and would never influence how the attack is flown.
    hut_radius = 70,
    gun_radius = 260,

    -- How far from the origin the camps are scattered. The player starts near
    -- the origin, so the inner figure leaves room to get flying before meeting
    -- anything; the map is 40 km across, so its edge is 20 km out.
    spread_min = 3000,
    spread_max = 9000,

    -- How much punishment the position as a whole takes before it falls. This
    -- is the camp's health, not any one building's: the buildings share it
    -- through the camp's single hitbox, so this is the number that decides how
    -- long an attack run has to be.
    camp_hp = 30,

    -- A gun vehicle is separately shootable and soft - a short burst kills one.
    gun_hp = 4,

    -- Fixes the layout: the same seed builds the same world every run, which
    -- makes the map learnable and a bug reproducible. Change it for a new one.
    seed = 20260803,

    -- Seconds between the last camp falling and a fresh set appearing
    -- elsewhere on the map.
    --
    -- Camps used to be built once and never again, which left the game in a
    -- strange state: the aircraft kept coming wave after wave while the ground
    -- war was simply over, and the objective counter sat at zero for the rest
    -- of the run. A pause first, so that finishing the last one is a moment
    -- rather than something immediately undone.
    rebuild_delay = 8,
}

-- A repeatable pseudo-random sequence, written out rather than using
-- math.random so this layout cannot be disturbed by anything else drawing
-- random numbers. Same seed, same world, every time.
local rngState = 0
local function nextRandom()
    -- A linear congruential generator, with the widely used Numerical Recipes
    -- constants; dividing by the modulus turns it into a 0..1 fraction.
    rngState = (rngState * 1664525 + 1013904223) % 4294967296
    return rngState / 4294967296
end

local function randomRange(lo, hi)
    return lo + (hi - lo) * nextRandom()
end

-- Place something on the ground at (x, z), with its ORIGIN on the surface.
--
-- That is the right default because an imported model's pivot is nearly always
-- at its base - both of the ones this camp uses measure that way - so a model
-- placed here simply stands on the terrain at any size. A `lift` is for the
-- exceptions: something whose origin is its middle, which is true of the plain
-- cube a missing model falls back to, and of the camp's own collision box.
--
-- Anything needing that correction applies it ITSELF, in its own onStart, where
-- its size is known. A lift written in here would be a second copy of a number
-- that lives in another file, wrong the moment either changed.
--
-- `model` names an entry in models.lua. A model whose FILE is missing falls back
-- to the entity's primitive shape and reports itself in the toolbar, so naming
-- one that has not been supplied yet costs nothing: the camp is boxes until the
-- file appears and vehicles afterwards, with no change here.
local function placeOnGround(name, x, z, lift, dirx, diry, dirz,
                             script, tag, hp, model)
    local y = Scene.groundHeight(x, z) + lift
    Scene.spawn(name, x, y, z, dirx, diry, dirz, script, tag, hp, model)
end

-- Seconds until the next set is built, once the field is clear. `nil` means
-- there are camps standing and nothing is pending.
local rebuildIn = nil

-- Build one complete set of camps. Called at the start, and again each time the
-- last one is destroyed.
--
-- The random sequence is NOT reseeded here, so each set carries on from where
-- the previous one stopped and lands somewhere new - while the whole run stays
-- reproducible from the one seed in the properties. Reseeding per set would
-- rebuild the same four camps in the same places for ever.
local function buildCamps()
    local P = properties

    for camp = 1, P.camp_count do
        -- Camps are placed by ANGLE AND DISTANCE from the origin rather than by
        -- picking x and z separately. Two independent coordinates crowd the
        -- corners of the square they are drawn from and leave the area around
        -- the player's start unusually empty.
        local ang  = randomRange(0, math.pi * 2)
        local dist = randomRange(P.spread_min, P.spread_max)
        local cx = math.cos(ang) * dist
        local cz = math.sin(ang) * dist

        -- THE CAMP ITSELF: an invisible entity carrying the collision box and
        -- the health for the whole position. Everything else in the camp is
        -- scenery hanging off it. It is placed at the height of the ground plus
        -- its own half-height, so its box stands ON the terrain rather than
        -- straddling it.
        --
        -- Spawned FIRST, so that if anything below were ever to look for its
        -- camp it would already be there.
        placeOnGround("Camp", cx, cz, 25, 0, 0, -1,
                      "assets/scripts/camp.lua", "camp", P.camp_hp)

        -- The headquarters, at the middle of it all.
        -- Tagged "hq" rather than "base", because structure.lua reads its own
        -- tag to decide whether it is the big command building or an
        -- outbuilding. camp.lua destroys both tags when the position falls.
        placeOnGround("Camp HQ", cx, cz, 0,
                      0, 0, -1, "assets/scripts/structure.lua", "hq", 0,
                      "hangar")

        -- Outbuildings, ringed around the HQ.
        for hut = 1, P.huts_per_camp do
            local a = randomRange(0, math.pi * 2)
            local r = randomRange(P.hut_radius * 0.5, P.hut_radius)
            placeOnGround("Camp Hut", cx + math.cos(a) * r, cz + math.sin(a) * r,
                          0, 0, 0, -1, "assets/scripts/structure.lua",
                          "base", 0, "hut")
        end

        -- The anti-aircraft vehicles, spread evenly around the camp rather than
        -- at random angles: an even ring has no gap to slip through, which is
        -- the entire reason a real battery is laid out that way.
        for gun = 1, P.guns_per_camp do
            local a = (gun - 1) * (math.pi * 2 / P.guns_per_camp)
                    + randomRange(-0.3, 0.3)          -- a little scatter, not a grid
            local r = randomRange(P.gun_radius * 0.7, P.gun_radius)
            -- Spawned facing OUTWARD from the camp, which is the way a gun
            -- defending it would be pointing, and horizontal so the spawn's
            -- look-at has a well defined answer.
            placeOnGround("AA Vehicle",
                          cx + math.cos(a) * r, cz + math.sin(a) * r,
                          0, math.cos(a), 0, math.sin(a),
                          "assets/scripts/aa_gun.lua", "aa", P.gun_hp,
                          "aa_vehicle")
        end
    end
end

function onStart(entity)
    rngState  = properties.seed
    rebuildIn = nil
    buildCamps()
end

function onUpdate(entity, dt)
    -- How many camps are still standing, for the HUD. Counted rather than
    -- remembered, so it cannot drift out of step with the world: a camp that is
    -- destroyed stops being counted the moment it is gone, whatever destroyed
    -- it and whether or not anything told this script about it.
    local standing = Scene.count("camp")
    Hud.set("camps", standing)

    -- A fresh set once the map is cleared, after a pause.
    --
    -- Deliberately NOT tied to the wave counter, even though the aircraft are.
    -- The two are separate fronts: waves arrive on their own schedule and camps
    -- replace themselves on theirs, so clearing the ground does not wait on the
    -- air being clear or the other way round. It also keeps this script from
    -- needing to know anything about gamemanager.lua.
    if standing > 0 then
        rebuildIn = nil
        return
    end

    -- The count is zero for the frame in which a set is built too, because a
    -- spawn is only queued and lands at the end of the frame. Resetting the
    -- timer after building rather than clearing it is what stops that reading
    -- as "still empty" and building a second set on top of the first.
    rebuildIn = (rebuildIn or properties.rebuild_delay) - dt
    if rebuildIn > 0 then return end

    buildCamps()
    rebuildIn = properties.rebuild_delay
end
