-- gun.lua
-- =============================================================================
-- A forward-firing gun, attached to a jet in addition to its flight script.
-- While the fire key is held it spawns bullet entities at a steady rate, each
-- launched from just ahead of the jet and pointed where the jet is facing.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
properties = {
    fire_rate = 0.2,   -- seconds between shots (smaller = faster fire)
    -- How far in front of the jet each bullet appears.
    --
    -- This must clear the jet's OWN COLLIDER, not just its model. A bullet is
    -- now a solid physical object, so one born inside the aircraft that fired
    -- it starts the frame overlapping it and is shoved aside instead of flying
    -- straight. The jet's collider is a capsule running nose to tail, so this
    -- needs to be past its front cap - check the green wireframe in the
    -- viewport and put the muzzle beyond it.
    --
    -- At the F-16's real size that capsule is 15 metres long and centred on the
    -- aircraft, so its front cap sits 7.5 metres ahead; 10 clears it with a
    -- couple of metres to spare. RAISE THIS AGAIN if the collider grows - a
    -- round appearing inside its own shooter does not travel.
    muzzle    = 10.0,
}

local cooldown = 0      -- seconds until the gun can fire again (runtime state)

-- Where this entity was on the previous frame, and whether we have one yet.
-- Comparing it with the current position gives the jet's velocity, which the
-- muzzle flash needs: an effect is born standing still, so on a jet moving 200
-- units a second a flash lasting a fifth of a second would be left 40 units
-- behind the nose. Handing the flash the jet's own motion keeps it at the gun.
local lx, ly, lz = 0, 0, 0
local has_last = false

function on_update(entity, dt)
    local P = properties
    cooldown = cooldown - dt

    -- Velocity is the change in position divided by the time it took. On the
    -- very first frame there is no previous position, so it counts as zero.
    local pos = entity.transform.position
    local vx, vy, vz = 0, 0, 0
    if has_last and dt > 0 then
        vx = (pos.x - lx) / dt
        vy = (pos.y - ly) / dt
        vz = (pos.z - lz) / dt
    end
    lx, ly, lz = pos.x, pos.y, pos.z
    has_last = true

    if input.key_down("SPACE") and cooldown <= 0 then
        cooldown = P.fire_rate
        local t = entity.transform
        local f = t:forward()
        local p = t.position
        local mx = p.x + f.x * P.muzzle
        local my = p.y + f.y * P.muzzle
        local mz = p.z + f.z * P.muzzle
        scene.spawn("Bullet", mx, my, mz, f.x, f.y, f.z,
            "assets/scripts/bullet.lua")
        -- A flash where the bullet leaves the gun, at the same point the bullet
        -- itself is created, carrying the jet's velocity so it stays at the
        -- nose instead of falling behind.
        fx.burst("muzzle", mx, my, mz, 1.0, vx, vy, vz)
        -- The report. Its pitch varies slightly per shot (set in sounds.lua), so
        -- sustained fire sounds like a gun rather than one repeated sample.
        -- Fired from the muzzle. On the player's own jet that is right beside
        -- the camera and so still loud and central, but the same script on
        -- another aircraft is heard from over there instead.
        audio.play_at("shot", mx, my, mz)
    end
end
