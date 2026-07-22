-- enemy.lua
-- =============================================================================
-- Flying-enemy AI that behaves like an aircraft, not a tracking drone. It
-- ALWAYS moves forward at a constant speed and can only TURN so fast, so when
-- it can't turn quickly enough it overshoots the player and loops back around
-- -- the feel of a dogfight. It fires only when its nose is lined up on the
-- player, and steers away from other enemies so a squadron doesn't merge.
--
-- Attach to an enemy entity (tag "enemy", plus a Health component). It hunts
-- the nearest entity tagged "player".
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
properties = {
    speed       = 11,    -- constant forward flight speed (world units/sec)
    turn_rate   = 65,    -- most degrees per second it can rotate toward the target
    fire_range  = 45,    -- only shoot when the player is at least this close
    fire_angle  = 12,    -- only shoot when the nose is within this many degrees
    fire_rate   = 1.0,   -- seconds between shots
    muzzle      = 2.0,   -- spawn bullets this far ahead of the enemy
    sep_range   = 8,     -- start avoiding other enemies within this distance
    sep_force   = 12,    -- how strongly to push apart from a crowding neighbor
}

local cooldown = 0       -- seconds until the enemy can fire again (runtime state)

function on_update(entity, dt)
    local t = entity.transform
    local P = properties

    -- Find the player (nearest entity tagged "player"; a huge radius just means
    -- "wherever it is"). Idle if there is no player.
    local target = scene.nearest("player", t.position.x, t.position.y, t.position.z, 100000)
    if target == nil then return end
    local tp = target.transform.position

    -- Turn toward the player, but no faster than turn_rate.
    t:rotate_toward(tp.x, tp.y, tp.z, P.turn_rate * dt)

    -- Always keep flying forward along the nose.
    t:translate_local(0, 0, -P.speed * dt)

    -- Separation: push away from the nearest OTHER enemy if too close, so the
    -- squadron spreads around the player instead of stacking up.
    local other = scene.nearest_other(entity, "enemy", P.sep_range)
    if other ~= nil then
        local op = other.transform.position
        local sx = t.position.x - op.x
        local sy = t.position.y - op.y
        local sz = t.position.z - op.z
        local d  = math.sqrt(sx * sx + sy * sy + sz * sz)
        if d > 0.001 then
            local strength = (P.sep_range - d) / P.sep_range
            local push = P.sep_force * strength * dt / d
            t.position.x = t.position.x + sx * push
            t.position.y = t.position.y + sy * push
            t.position.z = t.position.z + sz * push
        end
    end

    -- Distance to the player, and how far off the nose it is.
    local dx = tp.x - t.position.x
    local dy = tp.y - t.position.y
    local dz = tp.z - t.position.z
    local dist = math.sqrt(dx * dx + dy * dy + dz * dz)

    local f = t:forward()
    local inv = (dist > 0.0001) and (1.0 / dist) or 0.0
    local dot = f.x * dx * inv + f.y * dy * inv + f.z * dz * inv
    if dot >  1 then dot =  1 end
    if dot < -1 then dot = -1 end
    local aim_angle = math.deg(math.acos(dot))

    -- Fire when in range, roughly in front, and the gun is ready.
    cooldown = cooldown - dt
    if dist < P.fire_range and aim_angle < P.fire_angle and cooldown <= 0 then
        cooldown = P.fire_rate
        scene.spawn("EnemyBullet",
            t.position.x + f.x * P.muzzle, t.position.y + f.y * P.muzzle, t.position.z + f.z * P.muzzle,
            f.x, f.y, f.z,
            "assets/scripts/enemy_bullet.lua")
    end
end
