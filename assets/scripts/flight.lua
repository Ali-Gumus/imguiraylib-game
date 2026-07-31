-- flight.lua
-- =============================================================================
-- A simple arcade flight controller (the plane always flies forward; throttle
-- only changes how fast). This is the basic version; flight_sim.lua is the
-- momentum-based model. Attach to the jet.
-- Controls (Game panel focused): W/S pitch, A/D roll, Q/E yaw, Shift/Ctrl throttle.
-- If pitch feels inverted, swap the W/S signs below.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
properties = {
    pitch_rate = 55,   -- nose up/down speed (degrees per second)
    roll_rate  = 100,  -- roll speed (degrees per second)
    yaw_rate   = 40,   -- nose left/right speed (degrees per second)
    min_speed  = 3,    -- slowest the jet will fly
    max_speed  = 22,   -- fastest the jet will fly
    accel      = 8,    -- how fast the throttle changes speed
}

local speed = 8        -- current forward speed (runtime state)

function onUpdate(entity, dt)
    local t = entity.transform
    local P = properties

    -- Throttle raises/lowers the current speed, clamped to the min/max.
    if Input.keyDown("SHIFT") then speed = speed + P.accel * dt end
    if Input.keyDown("CTRL")  then speed = speed - P.accel * dt end
    if speed < P.min_speed then speed = P.min_speed end
    if speed > P.max_speed then speed = P.max_speed end

    -- Attitude: rotate around the jet's own axes. X = pitch, Z = roll, Y = yaw.
    if Input.keyDown("W") then t:rotate(1, 0, 0,  P.pitch_rate * dt) end
    if Input.keyDown("S") then t:rotate(1, 0, 0, -P.pitch_rate * dt) end
    if Input.keyDown("A") then t:rotate(0, 0, 1,  P.roll_rate  * dt) end
    if Input.keyDown("D") then t:rotate(0, 0, 1, -P.roll_rate  * dt) end
    if Input.keyDown("Q") then t:rotate(0, 1, 0,  P.yaw_rate   * dt) end
    if Input.keyDown("E") then t:rotate(0, 1, 0, -P.yaw_rate   * dt) end

    -- Constant forward thrust along the jet's facing (local -Z).
    t:translateLocal(0, 0, -speed * dt)
end
