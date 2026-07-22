-- flight_sim.lua
-- =============================================================================
-- An arcade AIRCRAFT flight model, attached to a jet entity.
--
-- The core idea: the plane has a single number for how fast it is moving
-- through the air ("airspeed"). Three things change that number every frame:
--   1. the engine (thrust) speeds it up,
--   2. air resistance (drag) slows it down,
--   3. climbing or diving trades airspeed against altitude.
-- The plane then moves forward (along its nose) by that airspeed. If airspeed
-- drops too low there is not enough airflow over the wings to stay up, so the
-- plane sinks -- a "stall".
--
-- The engine calls two functions on a script if they exist:
--   on_start(entity)      -- once, when play begins
--   on_update(entity, dt) -- every frame; dt = seconds since the last frame
-- `entity` is this plane. `entity.transform` holds its position and rotation.
-- =============================================================================

-- --- Handling: how quickly the nose responds to the controls -----------------
-- These are turn rates in DEGREES PER SECOND. Multiplying by dt below converts
-- them into "degrees this frame" so the plane turns at the same rate no matter
-- how fast the computer is running.
local pitch_rate = 55      -- nose up / down speed
local roll_rate  = 100     -- banking (rotating around the nose) speed
local yaw_rate   = 40      -- nose left / right speed

-- --- Flight model constants (the physics you can tune) -----------------------
local thrust      = 25     -- how hard the engine accelerates the plane (full throttle)
local drag         = 0.8   -- air resistance; larger = lower top speed
local gravity     = 20     -- how much airspeed a full climb costs per second
local stall_speed = 10     -- below this airspeed the wings stop holding the plane up
local sink_rate   = 1.5    -- how fast the plane drops once it has stalled

-- --- State: values that must be remembered from one frame to the next --------
-- Declared with `local` at the top of the file (not inside the function) so
-- they keep their value across frames instead of resetting every update.
local speed    = 18        -- current airspeed (starts already moving)
local throttle = 0.6       -- engine setting from 0 (idle) to 1 (full)

-- Called every frame. `entity` is the plane; `dt` is the frame time in seconds.
function on_update(entity, dt)
    -- Shorthand: `t` is this plane's transform (its position + orientation).
    local t = entity.transform

    -- --- Steering: rotate the NOSE ------------------------------------------
    -- t:rotate(ax, ay, az, degrees) turns the plane around its own axis
    -- (ax,ay,az) by `degrees`. (1,0,0) is the wing axis -> pitch. (0,0,1) is
    -- the nose axis -> roll. (0,1,0) is the vertical axis -> yaw. We rotate a
    -- little each frame (rate * dt) while the key is held.
    if input.key_down("W") then t:rotate(1, 0, 0,  pitch_rate * dt) end  -- nose up
    if input.key_down("S") then t:rotate(1, 0, 0, -pitch_rate * dt) end  -- nose down
    if input.key_down("A") then t:rotate(0, 0, 1,  roll_rate  * dt) end  -- roll left
    if input.key_down("D") then t:rotate(0, 0, 1, -roll_rate  * dt) end  -- roll right
    if input.key_down("Q") then t:rotate(0, 1, 0,  yaw_rate   * dt) end  -- yaw left
    if input.key_down("E") then t:rotate(0, 1, 0, -yaw_rate   * dt) end  -- yaw right

    -- --- Throttle: raise or lower the engine setting ------------------------
    -- Change throttle gradually while Shift/Ctrl are held, then clamp it into
    -- the valid 0..1 range so it can never go negative or above full power.
    if input.key_down("SHIFT") then throttle = throttle + 0.5 * dt end
    if input.key_down("CTRL")  then throttle = throttle - 0.5 * dt end
    if throttle < 0 then throttle = 0 end
    if throttle > 1 then throttle = 1 end

    -- --- Work out this frame's airspeed --------------------------------------
    -- forward() returns a unit vector pointing out of the plane's nose, in
    -- world space. Its Y part (f.y) tells us the climb angle: +1 = straight up,
    -- 0 = level, -1 = straight down.
    local f = t:forward()

    -- Airspeed changes from three effects, each scaled by dt:
    speed = speed + thrust * throttle * dt   -- 1) engine pushes speed up
    speed = speed - drag * speed * dt        -- 2) drag pulls it down (more when fast)
    speed = speed - gravity * f.y * dt       -- 3) climbing (f.y>0) costs speed;
                                             --    diving (f.y<0) gives speed back
    if speed < 0 then speed = 0 end          -- never let airspeed go negative

    -- --- Move the plane forward ---------------------------------------------
    -- Travel distance this frame = direction (f) * airspeed * dt. We build the
    -- movement per-axis so we can add extra downward motion below if stalled.
    local mx = f.x * speed
    local my = f.y * speed
    local mz = f.z * speed

    -- Stall: if airspeed fell below stall_speed, the wings can't hold the plane
    -- up, so pull it downward. The further below stall, the harder it drops.
    if speed < stall_speed then
        my = my - (stall_speed - speed) * sink_rate
    end

    -- Apply the movement to the plane's position (multiplying by dt turns the
    -- per-second movement into this-frame movement).
    t.position.x = t.position.x + mx * dt
    t.position.y = t.position.y + my * dt
    t.position.z = t.position.z + mz * dt
end
