-- terrain.lua
-- =============================================================================
-- Builds a ground reference out of blocks so that, while flying, you can sense
-- your speed, height and heading. Objects near you sweep past quickly while
-- far ones drift slowly (motion parallax), which is what gives a feeling of
-- depth and motion in an otherwise empty sky.
--
-- Attach to a single empty entity (it needs no Shape of its own). On the first
-- frame of play it spawns a grid of ground blocks plus a few tall towers as
-- landmarks, then does nothing more.
-- =============================================================================

local spacing = 15     -- world-unit gap between ground blocks
local half    = 7      -- grid runs from -half..half on each axis (so 15 x 15 blocks)

-- A one-time guard. Lua locals keep their value between frames, so this stays
-- true after the first build and the grid is never spawned twice.
local built = false

function on_update(entity, dt)
    if built then return end   -- already built: nothing to do
    built = true

    -- Lay down the ground grid. ix and iz step across the grid in both
    -- directions; multiplying by `spacing` turns them into world positions.
    for ix = -half, half do
        for iz = -half, half do
            local x = ix * spacing
            local z = iz * spacing
            -- Vary the height slightly with a smooth wave so the ground reads
            -- as gentle terrain instead of a perfectly flat checkerboard.
            local h = math.floor((math.sin(ix * 0.7) + math.cos(iz * 0.5)) * 1.5)
            scene.spawn_cube("Ground", x, h, z)
        end
    end

    -- A handful of tall towers at fixed spots act as landmarks you can steer
    -- by. Each tower is just a stack of blocks (same x,z, rising y).
    local towers = { {30, 30}, {-45, 20}, {60, -50}, {-30, -65}, {75, 40} }
    for _, tw in ipairs(towers) do
        for y = 0, 8 do
            scene.spawn_cube("Tower", tw[1], y, tw[2])
        end
    end
end
