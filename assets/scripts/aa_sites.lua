-- aa_sites.lua
-- =============================================================================
-- Lays out DEFENDED AREAS across the landscape: clusters of anti-aircraft guns
-- standing on the terrain, which shoot at the player when it flies near them.
--
-- Put this on the Game Manager, or on any entity that exists from the start. It
-- does its whole job in onStart and then does nothing for the rest of the game.
--
-- WHY CLUSTERS RATHER THAN GUNS SCATTERED EVENLY. Guns spread uniformly over a
-- 40 km map are just a constant low chance of being shot at wherever you fly,
-- which is weather, not a threat. Grouping them makes PLACES: a valley that is
-- dangerous and a ridge that is safe, so where you fly becomes a decision. It
-- also means the guns support each other - flying wide of one puts you inside
-- another's reach - which is exactly why real air defence is sited in belts
-- rather than sprinkled about.
--
-- HOW THEY END UP ON THE GROUND. Scene.groundHeight(x, z) reports the height of
-- the terrain surface at a point, so each gun is placed at that height rather
-- than at a guessed one. Without it every emplacement would either be buried in
-- a hillside or hanging in the air above a valley, and which one would depend on
-- the terrain seed.
-- =============================================================================

properties = {
    -- How many defended areas to place.
    site_count = 5,

    -- How many guns in each. Three is enough that flying through the middle is
    -- clearly worse than going round, without turning a site into a wall.
    guns_per_site = 3,

    -- How far apart the guns within one site are spread, in metres. Wider than
    -- their own size so they read as a battery rather than a stack, and well
    -- inside a single gun's range so they cover each other.
    site_radius = 300,

    -- How far from the origin the sites are scattered. The player starts near
    -- the origin, so the inner figure leaves room to take off and get flying
    -- before meeting anything, and the outer keeps them on the terrain - the
    -- map is 40 km across, so its edge is 20 km out.
    spread_min = 2500,
    spread_max = 9000,

    -- How much damage each gun takes to destroy. Tougher than an aircraft: it
    -- is a dug-in emplacement, and a target worth making a second pass at.
    gun_hp = 4,

    -- How high above the ground the gun sits. Its collider is a 6 metre ball
    -- centred on the entity, so lifting it by roughly that much leaves the
    -- emplacement resting on the surface instead of half sunk into it.
    stand_off = 5,

    -- Fixes the layout. With a seed the same map is defended the same way every
    -- run, which makes the world learnable and makes a bug reproducible; change
    -- it for a different arrangement.
    seed = 12345,
}

-- A repeatable pseudo-random sequence, written out rather than using math.random
-- so that this script's layout cannot be disturbed by anything else that happens
-- to draw random numbers. Same seed, same map, every time.
local rngState = 0
local function nextRandom()
    -- A linear congruential generator: the constants are the widely used ones
    -- from Numerical Recipes, and % 1 turns the result into a 0..1 fraction.
    rngState = (rngState * 1664525 + 1013904223) % 4294967296
    return rngState / 4294967296
end

local function randomRange(lo, hi)
    return lo + (hi - lo) * nextRandom()
end

function onStart(entity)
    local P = properties
    rngState = P.seed

    for site = 1, P.site_count do
        -- Sites are placed by ANGLE AND DISTANCE from the origin rather than by
        -- picking x and z independently. Two independent coordinates would
        -- cluster the sites towards the corners of the square they are drawn
        -- from, leaving the area near the player's start unusually empty and
        -- the diagonals crowded.
        local angle = randomRange(0, math.pi * 2)
        local dist  = randomRange(P.spread_min, P.spread_max)
        local cx = math.cos(angle) * dist
        local cz = math.sin(angle) * dist

        for gun = 1, P.guns_per_site do
            -- Spread the guns of one site around its centre, again by angle and
            -- distance, so a battery is a rough ring rather than a line.
            local ga = randomRange(0, math.pi * 2)
            local gd = randomRange(P.site_radius * 0.35, P.site_radius)
            local gx = cx + math.cos(ga) * gd
            local gz = cz + math.sin(ga) * gd

            -- Stand it on the ground. This is the call that makes the whole
            -- thing work on any terrain, at any seed, without hand placement.
            local gy = Scene.groundHeight(gx, gz) + P.stand_off

            -- Spawned facing straight up, which is where an idle anti-aircraft
            -- gun points and means its first slew is downward onto the target
            -- rather than a swing through the horizon.
            Scene.spawn("AA Gun", gx, gy, gz,
                        0, 1, 0,
                        "assets/scripts/aa_gun.lua",
                        "aa", P.gun_hp)
        end
    end
end
