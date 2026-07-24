-- bullet.lua
-- =============================================================================
-- A single player bullet. It flies straight forward, and if it passes close
-- enough to an entity tagged "enemy" it damages that enemy and removes itself.
-- If it hits nothing it disappears after a short lifetime.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
properties = {
    speed      = 70,    -- travel speed in world units per second
    life       = 2.0,   -- seconds before it self-destructs
    damage     = 1,     -- hit points removed from an enemy on impact
    hit_radius = 1.0,   -- how near an enemy must be to count as a hit
}

local age = 0           -- seconds this bullet has existed (runtime state)

function on_start(entity)
    -- Spawned entities start as a full unit cube; shrink to a small tracer.
    entity.transform.scale.x = 0.25
    entity.transform.scale.y = 0.25
    entity.transform.scale.z = 0.25
end

function on_update(entity, dt)
    local P = properties

    entity.transform:translate_local(0, 0, -P.speed * dt)   -- fly forward

    -- Hit test: any enemy within hit_radius of the bullet?
    local p = entity.transform.position
    -- scene.hit adds the enemy's own hitRadius to our reach, so a shot counts
    -- anywhere inside a large model, not only near its origin point.
    local target = scene.hit("enemy", p.x, p.y, p.z, P.hit_radius)
    if target ~= nil then
        -- Deal damage; the enemy awards score itself when it dies (enemy.lua's
        -- on_destroy), so the bullet stays a pure projectile.
        scene.damage(target, P.damage)
        scene.destroy(entity)
        return
    end

    age = age + dt
    if age > P.life then
        scene.destroy(entity)
    end
end
