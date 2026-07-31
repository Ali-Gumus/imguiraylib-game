-- flight_sim.lua
-- =============================================================================
-- A momentum-based aircraft flight model. Unlike a model that locks movement to
-- the nose, here the plane carries a real VELOCITY vector that is separate from
-- where the nose points. Aerodynamics then bends that velocity toward the nose
-- over time. This gives the feel of real flying:
--   * bank (roll) and pull back to TURN -- the turn also costs some height;
--   * momentum -- you can't change direction instantly, and a hard yaw makes
--     the plane skid sideways for a moment before it lines back up;
--   * fly too slowly and there isn't enough airflow to bend the velocity level,
--     so gravity wins and you sink (a stall) until you dive to regain speed;
--   * throttle sets your cruising speed (thrust versus drag).
--
-- Put this on the Jet. Controls (with the Game panel focused):
--   W / S        : pitch nose up / down    (swap the signs if it feels inverted)
--   A / D        : roll left / right
--   Q / E        : yaw left / right
--   SHIFT / CTRL : throttle up / down
-- =============================================================================

-- properties: a global table of tunable numbers. The engine shows each entry
-- as an editable field in the Inspector, so these can be adjusted per plane
-- (even while flying) without editing this file.
--
-- THESE NUMBERS ARE A REAL F-16C, IN THE ENGINE'S OWN UNITS.
-- One world unit is one metre, so an aircraft's real dimensions and speeds can
-- be used directly, and every value below is derived rather than dialled in by
-- feel. The reference airframe is an F-16C Block 50 at a combat weight of about
-- 12000 kg with one F110-GE-129 engine:
--
--   thrust  Acceleration, in metres per second squared, not a force. The engine
--           produces about 129000 newtons in full afterburner, and force divided
--           by mass is acceleration: 129000 / 12000 = 10.75. Reaching top speed
--           therefore takes the best part of a minute, which is how long a real
--           jet takes; it is not meant to feel like a car.
--
--   drag    Air resistance grows with the SQUARE of speed, so thrust and drag
--           balance at sqrt(thrust / drag) and that balance point IS the top
--           speed. Solving for a top speed of 685 metres per second - Mach 2 at
--           altitude, the F-16's limit - gives 10.8 / 685^2 = 0.000023. Change
--           either number and the top speed moves with it.
--
--   align   How hard the wings bend the velocity back towards the nose. This
--           has to fall as speed rises because the bend per frame is worked out
--           from speed x align: at the old airspeeds of around 100 the velocity
--           snapped to the nose in a fraction of a second, and at 685 it would
--           be instant, which removes the momentum and the stall along with it.
--           0.015 leaves roughly a tenth of a second of lag at top speed and
--           over a second near the stall, so a slow aircraft still sinks.
--
--   rates   A real F-16 rolls extremely fast - about 270 degrees per second -
--           while its PITCH rate is limited by what the airframe and the pilot
--           can survive, around 20 to 26 degrees per second in a hard turn.
--           Yaw is small on purpose: a jet's rudder coordinates a turn, it does
--           not steer the aircraft. Note the pitch rate here is a constant,
--           whereas a real one falls as speed rises (the same turn rate needs
--           more g the faster you go), so hard turns at top speed pull more g
--           than an airframe would allow.
--
--   cruise  The airspeed the aircraft is launched at. 250 is a realistic cruise
--           and is far above the roughly 57 metres per second it stalls at, so
--           it starts flying rather than falling.
properties = {
    pitch_rate = 25,        -- nose up/down (degrees per second)
    roll_rate  = 270,       -- roll (degrees per second)
    yaw_rate   = 8,         -- rudder yaw (degrees per second)
    thrust     = 10.8,      -- metres per second squared, full afterburner
    gravity    = 9.81,      -- metres per second squared, real gravity
    drag       = 0.000023,  -- gives a top speed of sqrt(10.8/0.000023) = 685 m/s
    align      = 0.015,     -- how strongly aerodynamics bend velocity to the nose
    cruise     = 250,       -- starting airspeed (metres per second)
}

-- State that must persist between frames.
local vx, vy, vz = 0, 0, 0    -- velocity in world space
local throttle   = 0.65       -- engine setting, 0..1

function onStart(entity)
    -- Launch already moving forward at the cruise speed, so the plane is
    -- flying (not stalling) from the first frame.
    local f = entity.transform:forward()
    local c = properties.cruise
    vx, vy, vz = f.x * c, f.y * c, f.z * c

    -- Start the engine note. It runs for as long as the jet exists; the volume
    -- and pitch are set every frame below from the throttle.
    Audio.loopStart("jet")
end

function onUpdate(entity, dt)
    local t = entity.transform
    local P = properties      -- shorthand for the tunable values

    -- --- Steer the nose. Note this only rotates the aircraft; the velocity
    --     reacts through the aerodynamics below, not instantly. ---------------
    if Input.keyDown("W") then t:rotate(1, 0, 0,  P.pitch_rate * dt) end
    if Input.keyDown("S") then t:rotate(1, 0, 0, -P.pitch_rate * dt) end
    if Input.keyDown("A") then t:rotate(0, 0, 1,  P.roll_rate  * dt) end
    if Input.keyDown("D") then t:rotate(0, 0, 1, -P.roll_rate  * dt) end
    if Input.keyDown("Q") then t:rotate(0, 1, 0,  P.yaw_rate   * dt) end
    if Input.keyDown("E") then t:rotate(0, 1, 0, -P.yaw_rate   * dt) end

    -- --- Throttle -----------------------------------------------------------
    if Input.keyDown("SHIFT") then throttle = throttle + 0.4 * dt end
    if Input.keyDown("CTRL")  then throttle = throttle - 0.4 * dt end
    if throttle < 0 then throttle = 0 end
    if throttle > 1 then throttle = 1 end

    -- --- Forces acting on the velocity --------------------------------------
    local f = t:forward()                                  -- the nose direction
    local speed = math.sqrt(vx * vx + vy * vy + vz * vz)

    -- Thrust pushes the velocity forward along the nose.
    vx = vx + f.x * P.thrust * throttle * dt
    vy = vy + f.y * P.thrust * throttle * dt
    vz = vz + f.z * P.thrust * throttle * dt

    -- Gravity pulls the velocity down.
    vy = vy - P.gravity * dt

    -- Drag opposes the velocity and grows with speed, which caps the top speed.
    local d = P.drag * speed * dt
    vx = vx - vx * d
    vy = vy - vy * d
    vz = vz - vz * d

    -- Aerodynamics: bend the velocity toward the way the nose points. This is
    -- what a wing does -- it resists sideways/vertical motion and makes the
    -- plane fly where it is aimed. The bend is stronger at higher speed (more
    -- airflow), so when slow it can't fight gravity and the plane sinks.
    speed = math.sqrt(vx * vx + vy * vy + vz * vz)          -- recompute after the forces
    local desx, desy, desz = f.x * speed, f.y * speed, f.z * speed   -- same speed, nose direction
    local a = 1 - math.exp(-speed * P.align * dt)           -- 0 = no bending, 1 = fully aligned
    vx = vx + (desx - vx) * a
    vy = vy + (desy - vy) * a
    vz = vz + (desz - vz) * a

    -- --- Move by the velocity ----------------------------------------------
    t.position.x = t.position.x + vx * dt
    t.position.y = t.position.y + vy * dt
    t.position.z = t.position.z + vz * dt

    -- Publish flight values for the HUD to display.
    Hud.set("throttle", throttle)   -- 0..1 engine power
    Hud.set("speed", speed)         -- current airspeed

    -- Tie the engine note to the throttle. Both volume and pitch rise with
    -- power, which is what makes opening the throttle *feel* like acceleration
    -- rather than just changing a number on the HUD. The ranges are deliberately
    -- narrow: an engine at idle is quieter and lower, not silent and inaudible.
    --
    -- Positioned at the aircraft, and updated every frame because the aircraft
    -- moves: with a chase camera the engine sits just ahead of the listener, so
    -- it stays loud and centred, but the same script on an enemy jet is heard
    -- from wherever that jet is.
    local ep = entity.transform.position
    Audio.loopAt("jet", ep.x, ep.y, ep.z,
                  1 + throttle * 1, 1.5 + throttle * 1)
end
