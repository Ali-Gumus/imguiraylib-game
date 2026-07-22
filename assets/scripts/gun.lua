-- gun.lua
-- =============================================================================
-- A forward-firing gun, attached to a jet in addition to its flight script.
-- While the fire key is held it spawns bullet entities at a steady rate, each
-- launched from just ahead of the jet and pointed where the jet is facing.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
properties = {
    fire_rate = 0.12,   -- seconds between shots (smaller = faster fire)
    muzzle    = 3.0,    -- how far in front of the jet each bullet appears
}

local cooldown = 0      -- seconds until the gun can fire again (runtime state)

function on_update(entity, dt)
    local P = properties
    cooldown = cooldown - dt

    if input.key_down("SPACE") and cooldown <= 0 then
        cooldown = P.fire_rate
        local t = entity.transform
        local f = t:forward()
        local p = t.position
        scene.spawn("Bullet",
            p.x + f.x * P.muzzle, p.y + f.y * P.muzzle, p.z + f.z * P.muzzle,
            f.x, f.y, f.z,
            "assets/scripts/bullet.lua")
    end
end
