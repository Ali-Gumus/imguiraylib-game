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

-- BOTH things this script creates need the jet's own velocity:
--
--   * The muzzle flash. An effect is born standing still, so on a jet moving
--     200 metres a second a flash lasting a fifth of a second would be left 40
--     metres behind the nose. Handing it the jet's motion keeps it at the gun.
--
--   * The bullet. A gun's muzzle velocity is quoted RELATIVE TO THE GUN, so a
--     round leaves a moving aircraft at its muzzle speed plus the aircraft's.
--     Measured against the ground instead, the faster the jet flies the less
--     its own fire outruns it - and a jet faster than its muzzle velocity would
--     overtake its own rounds.
--
-- ASK FOR THE VELOCITY, DO NOT WORK IT OUT. `entity.velocity` is measured by
-- the engine at one fixed point in every frame, so it is right whatever order
-- this script's component sits in.
--
-- This used to be (this position - last position) / dt here, which looks
-- obviously right and is quietly wrong: the displacement was produced by a
-- DIFFERENT frame's dt than the one it was divided by. Steady frames hid it.
-- Measured at 316 m/s, a frame 15% longer than the one before under-read the
-- speed by 15% and left the muzzle flash 9 m behind the nose, while one that
-- much shorter over-read it and threw the flash 12 m in front - and the frames
-- that wobble most are the ones a shot is fired in, because spawning a bullet
-- is the most expensive thing that happens in a frame.
function onUpdate(entity, dt)
    local P = properties
    cooldown = cooldown - dt

    local v = entity.velocity
    local vx, vy, vz = v.x, v.y, v.z

    if Input.keyDown("SPACE") and cooldown <= 0 then
        cooldown = P.fire_rate
        local t = entity.transform
        local f = t:forward()
        local p = t.position

        -- WHERE THE JET WILL BE AT THE END OF THIS FRAME, not where it is now.
        --
        -- The flight model is a component further down this entity's list - that
        -- is where Add Component puts a new one - so it has not run yet when
        -- this does. The position read above is therefore where the aircraft was
        -- when the LAST frame ended, and by the time the round actually exists
        -- the jet has moved on by a whole frame.
        --
        -- That distance is speed x frame time, so it grows as the frame rate
        -- falls, which is why the problem comes and goes with the frame rate
        -- rather than staying put. Measured at 317 m/s: 5.3 m at 60 fps, 10.6 m
        -- at 30, 15.9 m at 20. The muzzle only stands 10 m out in front, so
        -- below about 30 fps the round is born BEHIND the nose - inside the
        -- jet's own 15 m collider, where being solid means it is shoved aside
        -- instead of flying, which is the round going somewhere strange.
        --
        -- Stepping forward by one frame of the jet's own velocity lands on where
        -- it will actually be. The same measurement with this applied: 0.01 m at
        -- 60 fps, and never worse than 1.3 m.
        --
        -- IF THE COMPONENT ORDER IS EVER CHANGED so the flight model runs BEFORE
        -- this script, the position read above would already be current and this
        -- would overshoot by the same amount it now corrects.
        local ex = p.x + vx * dt
        local ey = p.y + vy * dt
        local ez = p.z + vz * dt

        local mx = ex + f.x * P.muzzle
        local my = ey + f.y * P.muzzle
        local mz = ez + f.z * P.muzzle
        -- The three nils are the tag, health and model a bullet does not want;
        -- the velocity has to come after them because it was added to the call
        -- last, and renumbering the arguments would have broken every existing
        -- caller and every graph that generates one.
        Scene.spawn("Bullet", mx, my, mz, f.x, f.y, f.z,
            "assets/scripts/bullet.lua", nil, nil, nil, vx, vy, vz)
        -- A flash where the bullet leaves the gun, at the same point the bullet
        -- itself is created, carrying the jet's velocity so it stays at the
        -- nose instead of falling behind.
        Fx.burst("muzzle", mx, my, mz, 1.0, vx, vy, vz)
        -- The report. Its pitch varies slightly per shot (set in sounds.lua), so
        -- sustained fire sounds like a gun rather than one repeated sample.
        -- Fired from the muzzle. On the player's own jet that is right beside
        -- the camera and so still loud and central, but the same script on
        -- another aircraft is heard from over there instead.
        Audio.playAt("shot", mx, my, mz)
    end
end
