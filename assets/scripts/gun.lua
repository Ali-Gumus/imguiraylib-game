-- gun.lua
-- =============================================================================
-- A forward-firing gun, attached to a jet (in addition to its flight script).
-- While the fire key is held it spawns bullet entities at a steady rate, each
-- one launched from just ahead of the jet and pointed where the jet is facing.
--
-- The engine calls on_update(entity, dt) every frame, where `entity` is the
-- jet and `dt` is the seconds elapsed since the previous frame.
-- =============================================================================

-- How many seconds must pass between shots. Smaller = faster fire rate.
local fire_rate = 0.12

-- How far in front of the jet (in world units) each bullet appears, so bullets
-- start ahead of the plane instead of inside it.
local muzzle = 3.0

-- Countdown timer, in seconds, until the gun is allowed to fire again.
-- Remembered across frames because it is declared once, here, with `local`.
local cooldown = 0

function on_update(entity, dt)
    -- Count the timer down by the frame time. Once it reaches zero the gun is
    -- ready; while it is above zero the gun is still recovering from the last shot.
    cooldown = cooldown - dt

    -- Fire only when the key is held AND the cooldown has finished.
    if input.key_down("SPACE") and cooldown <= 0 then
        -- Reset the timer so the next shot waits one full interval.
        cooldown = fire_rate

        local t = entity.transform      -- the jet's position + orientation
        local f = t:forward()           -- unit vector out of the jet's nose (world space)
        local p = t.position            -- the jet's current position

        -- Create a bullet. Arguments are:
        --   name        : "Bullet" (shown in the editor's list)
        --   x, y, z      : spawn position = jet position pushed `muzzle` units forward
        --   dx, dy, dz   : the direction the bullet should face (the jet's forward)
        --   script       : the behavior the new bullet runs on its own
        scene.spawn("Bullet",
            p.x + f.x * muzzle, p.y + f.y * muzzle, p.z + f.z * muzzle,
            f.x, f.y, f.z,
            "assets/scripts/bullet.lua")
    end
end
