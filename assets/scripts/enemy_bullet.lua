-- enemy_bullet.lua
-- =============================================================================
-- A bullet fired by an enemy. Same as the player's bullet, except it looks for
-- the "player" tag instead of "enemy", so enemy fire hurts the player and not
-- other enemies.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
properties = {
    speed      = 55,    -- travel speed in world units per second
    life       = 3.0,   -- seconds before it self-destructs
    damage     = 1,     -- hit points removed from the player on impact
    hit_radius = 1.2,   -- how near the player must be to count as a hit
}

local age = 0           -- seconds this bullet has existed (runtime state)

function on_start(entity)
    entity.transform.scale.x = 0.25
    entity.transform.scale.y = 0.25
    entity.transform.scale.z = 0.25
end

function on_update(entity, dt)
    local P = properties

    entity.transform:translate_local(0, 0, -P.speed * dt)

    local p = entity.transform.position
    local target = scene.nearest("player", p.x, p.y, p.z, P.hit_radius)
    if target ~= nil then
        scene.damage(target, P.damage)
        scene.destroy(entity)
        return
    end

    age = age + dt
    if age > P.life then
        scene.destroy(entity)
    end
end
