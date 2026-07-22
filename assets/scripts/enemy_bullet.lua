-- enemy_bullet.lua
-- =============================================================================
-- A bullet fired by an enemy. It behaves exactly like the player's bullet
-- (fly straight, self-destruct after a lifetime) except it looks for the
-- "player" tag instead of "enemy", so enemy fire hurts the player and not
-- other enemies. Spawned by enemy.lua already facing the shot direction.
-- =============================================================================

local speed      = 55    -- travel speed in world units per second
local life       = 3.0   -- seconds before it self-destructs if it misses
local damage     = 1     -- hit points removed from the player on impact
local hit_radius = 1.2   -- how near the player must be to count as a hit
local age        = 0

function on_start(entity)
    -- shrink to a small tracer (spawned entities start as a full unit cube)
    entity.transform.scale.x = 0.25
    entity.transform.scale.y = 0.25
    entity.transform.scale.z = 0.25
end

function on_update(entity, dt)
    -- fly forward along the bullet's own facing
    entity.transform:translate_local(0, 0, -speed * dt)

    -- hit test against the nearest "player"-tagged entity
    local p = entity.transform.position
    local target = scene.nearest("player", p.x, p.y, p.z, hit_radius)
    if target ~= nil then
        scene.damage(target, damage)   -- reduce the player's health
        scene.destroy(entity)          -- the bullet is spent
        return
    end

    -- missed this frame: age out after `life` seconds
    age = age + dt
    if age > life then
        scene.destroy(entity)
    end
end
