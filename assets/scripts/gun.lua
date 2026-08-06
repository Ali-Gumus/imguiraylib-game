-- gun.lua
-- =============================================================================
-- A forward-firing gun, attached to a jet in addition to its flight script.
-- While the fire key is held it spawns bullet entities at a steady rate, each
-- launched from just ahead of the jet and pointed where the jet is facing.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
properties = {
    fire_rate = 0.2,   -- seconds between shots (smaller = faster fire)

    -- WHERE THE GUN ACTUALLY IS, as an offset from the entity's position in the
    -- aircraft's OWN axes, metres. This is what you see: the muzzle flash and
    -- the report come from here.
    --
    --   muzzle_forward - toward the nose
    --   muzzle_up      - above the centreline
    --   muzzle_right   - to starboard; NEGATIVE puts it to port
    --
    -- The defaults are an F-16's M61 Vulcan, which is not on the centreline and
    -- never has been: it sits in the PORT wing root, its muzzle port high on the
    -- left side of the fuselage just ahead of the wing. On a 15 m airframe whose
    -- origin is its middle, that is roughly two metres forward of centre, a bit
    -- over half a metre to the left, and half a metre up.
    --
    -- Tune by eye against the model, the way aa_gun.lua's muzzle is tuned.
    muzzle_forward = 2.3,
    muzzle_up      = 0.450,
    muzzle_right   = -0.8,

    -- HOW FAR ALONG THE LINE OF FIRE THE ROUND IS ACTUALLY CREATED, metres.
    --
    -- This exists because of a collision problem that has no other cheap fix,
    -- and it is worth understanding rather than tuning blindly.
    --
    -- A bullet is a solid physical object and so is the jet. The real gun port
    -- is INSIDE the aircraft's own collision capsule - it is a gun buried in a
    -- fuselage, so of course it is - and a round born inside another solid body
    -- starts the frame overlapping it and gets shoved aside instead of flying.
    -- The symptom is rounds that curve away or simply do not appear.
    --
    -- So the flash stays at the real gun port and the ROUND is created further
    -- along the same line, clear of the airframe. That is not a fudge: the gun
    -- is boresighted along the nose, so this is the same line of fire either
    -- way - the round merely skips its first few metres, which at a thousand
    -- metres a second is three thousandths of a second, and it is travelling
    -- far too fast to see there anyway.
    --
    -- The jet's capsule is 12.6 m long with a 1.2 m radius, centred on the
    -- aircraft, so it reaches 7.5 m forward. From a muzzle 2 m forward, 8 more
    -- puts the round at 10 m - clear, with the same margin the old single
    -- distance had. RAISE THIS IF THE COLLIDER GROWS.
    --
    -- The proper fix is collision filtering, so a projectile simply ignores the
    -- entity that fired it. Until that exists, this is the workaround.
    spawn_clearance = 8.0,

    -- HOW MANY ROUNDS THE AIRCRAFT CARRIES. The F-16's M61A1 holds 511.
    --
    -- Zero means UNLIMITED, which is the arcade option and the same convention
    -- the JSBSim component's fuel uses - a magazine you can empty is a real
    -- constraint and deserves to be opt-in rather than sprung on a scene that
    -- was tuned without one.
    ammo_capacity = 511,

    -- HOW MANY ROUNDS EACH VISIBLE SHOT COSTS, and the reason this exists is
    -- worth reading before changing either number.
    --
    -- A real M61 fires a HUNDRED rounds a second. This gun spawns one tracer
    -- every `fire_rate` seconds - five a second - because a hundred entities a
    -- second would be absurd and invisible. So one tracer here stands for a
    -- short burst there, and if each cost a single round the 511-round magazine
    -- would last a hundred seconds of held trigger instead of the five it
    -- really gives.
    --
    -- Twenty is the ratio between the two rates (100 / 5), which makes the
    -- magazine last about as long as the real one: 511 / 20 is 25 shots, and at
    -- five a second that is five seconds of trigger. Lower it for a longer,
    -- more forgiving magazine.
    rounds_per_shot = 20,
}

local cooldown = 0      -- seconds until the gun can fire again (runtime state)
local ammo     = 0      -- rounds left; see onStart

function onStart(entity)
    ammo = properties.ammo_capacity
end

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

    -- Publish the magazine for the HUD. Done every frame rather than only when
    -- a shot is fired, so the readout appears as soon as the run starts instead
    -- of after the first trigger pull.
    --
    -- An unlimited magazine publishes -1, which is the sentinel the HUD already
    -- uses to mean "there is nothing to show here" - so switching ammo off
    -- hides the counter rather than parking it at a number that never moves.
    local unlimited = P.ammo_capacity <= 0
    Hud.set("ammo", unlimited and -1 or ammo)
    Hud.set("ammo_max", unlimited and -1 or P.ammo_capacity)

    -- An empty magazine simply does not fire. No click, no dry-fire sound:
    -- there is no sound file for one, and a missing sound is silent anyway.
    if not unlimited and ammo <= 0 then return end

    if Input.keyDown("SPACE") and cooldown <= 0 then
        cooldown = P.fire_rate

        if not unlimited then
            ammo = ammo - P.rounds_per_shot
            if ammo < 0 then ammo = 0 end
        end
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

        -- The gun port, in world space. The offset is written in the aircraft's
        -- own axes, so it is applied by walking that far along each of them -
        -- which is what keeps the gun on the same spot of the airframe however
        -- the aircraft is banked or pitched. Same construction aa_gun.lua uses.
        local r = t:right()
        local u = t:up()
        local mx = ex + f.x * P.muzzle_forward + u.x * P.muzzle_up + r.x * P.muzzle_right
        local my = ey + f.y * P.muzzle_forward + u.y * P.muzzle_up + r.y * P.muzzle_right
        local mz = ez + f.z * P.muzzle_forward + u.z * P.muzzle_up + r.z * P.muzzle_right

        -- The round starts further along the same line, clear of the aircraft's
        -- own collider - see spawn_clearance above for why it cannot start at
        -- the gun itself.
        local bx = mx + f.x * P.spawn_clearance
        local by = my + f.y * P.spawn_clearance
        local bz = mz + f.z * P.spawn_clearance

        -- The three nils are the tag, health and model a bullet does not want;
        -- the velocity has to come after them because it was added to the call
        -- last, and renumbering the arguments would have broken every existing
        -- caller and every graph that generates one.
        Scene.spawn("Bullet", bx, by, bz, f.x, f.y, f.z,
            "assets/scripts/bullet.lua", nil, nil, nil, vx, vy, vz)
        -- The flash belongs at the GUN, not where the round clears the airframe.
        -- It carries the jet's velocity so it stays on the muzzle instead of
        -- falling behind.
        Fx.burst("muzzle", mx, my, mz, 0.2, vx, vy, vz)
        -- The report. Its pitch varies slightly per shot (set in sounds.lua), so
        -- sustained fire sounds like a gun rather than one repeated sample.
        -- Fired from the muzzle. On the player's own jet that is right beside
        -- the camera and so still loud and central, but the same script on
        -- another aircraft is heard from over there instead.
        Audio.playAt("shot", mx, my, mz)
    end
end
