-- flight.lua — arcade jet controls. Attach to the jet entity.
-- Controls (Game panel must be hovered/focused so input reaches the game):
--   W / S        : pitch nose up / down
--   A / D        : roll left / right
--   Q / E        : yaw left / right
--   SHIFT / CTRL : throttle up / down
-- The jet ALWAYS flies forward; throttle only changes how fast.
-- If pitch feels inverted, swap the W/S signs below.

-- Tunables (edit + Reload to feel different handling). These become
-- Inspector fields once script-properties (feature #12) land.
local pitch_rate = 55     -- degrees/second
local roll_rate  = 100
local yaw_rate   = 40

local speed      = 8      -- current forward speed (units/second)
local min_speed  = 3
local max_speed  = 22
local accel      = 8      -- throttle change per second

function on_update(entity, dt)
    local t = entity.transform

    -- Throttle
    if input.key_down("SHIFT") then speed = speed + accel * dt end
    if input.key_down("CTRL")  then speed = speed - accel * dt end
    if speed < min_speed then speed = min_speed end
    if speed > max_speed then speed = max_speed end

    -- Attitude: rotate around the jet's OWN axes (quaternion-safe, so
    -- loops and rolls never gimbal-lock). X = pitch, Z = roll, Y = yaw.
    if input.key_down("W") then t:rotate(1, 0, 0,  pitch_rate * dt) end
    if input.key_down("S") then t:rotate(1, 0, 0, -pitch_rate * dt) end
    if input.key_down("A") then t:rotate(0, 0, 1,  roll_rate  * dt) end
    if input.key_down("D") then t:rotate(0, 0, 1, -roll_rate  * dt) end
    if input.key_down("Q") then t:rotate(0, 1, 0,  yaw_rate   * dt) end
    if input.key_down("E") then t:rotate(0, 1, 0, -yaw_rate   * dt) end

    -- Constant forward thrust along the jet's facing (local -Z).
    t:translate_local(0, 0, -speed * dt)
end
