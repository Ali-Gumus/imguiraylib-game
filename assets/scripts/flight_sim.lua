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
properties = {
    pitch_rate = 50,     -- nose up/down speed (degrees per second)
    roll_rate  = 110,    -- roll speed (degrees per second)
    yaw_rate   = 35,     -- nose left/right speed (degrees per second)
    thrust     = 32,     -- forward acceleration at full throttle
    gravity    = 13,     -- downward acceleration
    drag       = 0.020,  -- air resistance; with thrust this sets the top speed
    align      = 0.10,   -- how strongly aerodynamics bend velocity toward the nose
    cruise     = 20,     -- starting airspeed
}

-- State that must persist between frames.
local vx, vy, vz = 0, 0, 0    -- velocity in world space
local throttle   = 0.65       -- engine setting, 0..1

function on_start(entity)
    -- Launch already moving forward at the cruise speed, so the plane is
    -- flying (not stalling) from the first frame.
    local f = entity.transform:forward()
    local c = properties.cruise
    vx, vy, vz = f.x * c, f.y * c, f.z * c
end

function on_update(entity, dt)
    local t = entity.transform
    local P = properties      -- shorthand for the tunable values

    -- --- Steer the nose. Note this only rotates the aircraft; the velocity
    --     reacts through the aerodynamics below, not instantly. ---------------
    if input.key_down("W") then t:rotate(1, 0, 0,  P.pitch_rate * dt) end
    if input.key_down("S") then t:rotate(1, 0, 0, -P.pitch_rate * dt) end
    if input.key_down("A") then t:rotate(0, 0, 1,  P.roll_rate  * dt) end
    if input.key_down("D") then t:rotate(0, 0, 1, -P.roll_rate  * dt) end
    if input.key_down("Q") then t:rotate(0, 1, 0,  P.yaw_rate   * dt) end
    if input.key_down("E") then t:rotate(0, 1, 0, -P.yaw_rate   * dt) end

    -- --- Throttle -----------------------------------------------------------
    if input.key_down("SHIFT") then throttle = throttle + 0.4 * dt end
    if input.key_down("CTRL")  then throttle = throttle - 0.4 * dt end
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
    hud.set("throttle", throttle)   -- 0..1 engine power
    hud.set("speed", speed)         -- current airspeed
end
