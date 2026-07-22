-- terrain.lua
-- =============================================================================
-- Builds a ground reference out of blocks so that, while flying, you can sense
-- speed, height and heading (near blocks sweep past faster than far ones).
--
-- Attach to a single empty entity. On the first frame of play it spawns a grid
-- of ground blocks plus a few tall towers as landmarks, then does nothing more.
-- Note: the grid is built once, so changing the values below takes effect only
-- on a fresh play, not live.
-- =============================================================================

-- Tunable numbers, shown as editable fields in the Inspector.
properties = {
    spacing = 15,   -- world-unit gap between ground blocks
    half    = 7,    -- grid runs from -half..half on each axis
}

local built = false     -- one-time guard (runtime state)

function on_update(entity, dt)
    if built then return end
    built = true

    local spacing = properties.spacing
    local half    = math.floor(properties.half)

    for ix = -half, half do
        for iz = -half, half do
            local x = ix * spacing
            local z = iz * spacing
            -- Slight height variation so it reads as terrain, not a flat grid.
            local h = math.floor((math.sin(ix * 0.7) + math.cos(iz * 0.5)) * 1.5)
            scene.spawn_cube("Ground", x, h, z)
        end
    end

    -- A few tall towers as landmarks (stacks of blocks at fixed spots).
    local towers = { {30, 30}, {-45, 20}, {60, -50}, {-30, -65}, {75, 40} }
    for _, tw in ipairs(towers) do
        for y = 0, 8 do
            scene.spawn_cube("Tower", tw[1], y, tw[2])
        end
    end
end
