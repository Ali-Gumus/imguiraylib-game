-- enemy.lua
-- =============================================================================
-- Flying-enemy AI that behaves like an aircraft, not a tracking drone. It
-- ALWAYS moves forward at a constant speed and can only TURN so fast, so when
-- it can't turn quickly enough it overshoots the player and has to loop back
-- around -- the feel of a dogfight. It fires only when its nose is actually
-- lined up with the player.
--
-- Attach to an enemy entity (tag "enemy", plus a Health component so the
-- player can shoot it down). It hunts the nearest entity tagged "player".
-- =============================================================================

local speed       = 11     -- constant forward flight speed (world units/sec)
local turn_rate   = 65     -- most degrees per second it can rotate toward the target
local fire_range  = 45     -- only shoot when the player is at least this close
local fire_angle  = 12     -- only shoot when the nose is within this many degrees of the player
local fire_rate   = 1.0    -- seconds between shots
local muzzle      = 2.0    -- spawn bullets this far ahead of the enemy

local sep_range   = 8      -- start avoiding other enemies within this distance
local sep_force   = 12     -- how strongly to push apart (world units/sec at closest)

local cooldown = 0

function on_update(entity, dt)
    local t = entity.transform

    -- Find the player (nearest entity tagged "player"; a huge radius just
    -- means "wherever it is"). Idle if there is no player.
    local target = scene.nearest("player", t.position.x, t.position.y, t.position.z, 100000)
    if target == nil then return end
    local tp = target.transform.position

    -- Turn toward the player, but no faster than turn_rate. This gradual turn
    -- is what makes the enemy fly rather than snap around.
    t:rotate_toward(tp.x, tp.y, tp.z, turn_rate * dt)

    -- Always keep flying forward along the nose (a jet never stops in the air).
    t:translate_local(0, 0, -speed * dt)

    -- Separation: keep the squadron from stacking into one point. Find the
    -- nearest OTHER enemy nearby and push directly away from it. The push gets
    -- stronger the closer they are (strength goes from 0 at sep_range to 1 when
    -- touching), so enemies spread out around the player instead of merging.
    local other = scene.nearest_other(entity, "enemy", sep_range)
    if other ~= nil then
        local op = other.transform.position
        local sx = t.position.x - op.x
        local sy = t.position.y - op.y
        local sz = t.position.z - op.z
        local d  = math.sqrt(sx * sx + sy * sy + sz * sz)
        if d > 0.001 then
            local strength = (sep_range - d) / sep_range      -- 1 = touching, 0 = at edge
            local push = sep_force * strength * dt / d         -- (÷d normalizes the direction)
            t.position.x = t.position.x + sx * push
            t.position.y = t.position.y + sy * push
            t.position.z = t.position.z + sz * push
        end
    end

    -- Work out the distance to the player and how far off the nose it is, to
    -- decide whether a shot would actually hit.
    local dx = tp.x - t.position.x
    local dy = tp.y - t.position.y
    local dz = tp.z - t.position.z
    local dist = math.sqrt(dx * dx + dy * dy + dz * dz)

    -- Angle between the nose (forward) and the direction to the player.
    local f = t:forward()
    local inv = (dist > 0.0001) and (1.0 / dist) or 0.0     -- 1/dist to normalize (dx,dy,dz)
    local dot = f.x * dx * inv + f.y * dy * inv + f.z * dz * inv
    if dot >  1 then dot =  1 end                            -- clamp for acos safety
    if dot < -1 then dot = -1 end
    local aim_angle = math.deg(math.acos(dot))              -- 0 = pointing right at the player

    -- Fire when the player is in range, roughly in front, and the gun is ready.
    cooldown = cooldown - dt
    if dist < fire_range and aim_angle < fire_angle and cooldown <= 0 then
        cooldown = fire_rate
        scene.spawn("EnemyBullet",
            t.position.x + f.x * muzzle, t.position.y + f.y * muzzle, t.position.z + f.z * muzzle,
            f.x, f.y, f.z,
            "assets/scripts/enemy_bullet.lua")
    end
end
