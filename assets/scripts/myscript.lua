-- GENERATED from a node graph. Edit the GRAPH, not this file --

local vx = 0
local vy = 0
local vz = 0
local throttle = 0.65
local speed = 0
local d = 0
local a = 0

function on_start(entity)
    vx = (entity.transform:forward().x * 20)
    vy = (entity.transform:forward().y * 20)
    vz = (entity.transform:forward().z * 20)
end

function on_update(entity, dt)
    if input.key_down("W") then
    entity.transform:rotate(1, 0, 0, (50 * dt))
    else
    end
    if input.key_down("S") then
    entity.transform:rotate(1, 0, 0, (-50 * dt))
    else
    end
    if input.key_down("A") then
    entity.transform:rotate(0, 0, 1, (110 * dt))
    else
    end
    if input.key_down("D") then
    entity.transform:rotate(0, 0, 1, (-110 * dt))
    else
    end
    if input.key_down("Q") then
    entity.transform:rotate(0, 1, 0, (35 * dt))
    else
    end
    if input.key_down("E") then
    entity.transform:rotate(0, 1, 0, (-35 * dt))
    else
    end
    if input.key_down("SHIFT") then
    throttle = (throttle + (0.4 * dt))
    else
    end
    if input.key_down("CTRL") then
    throttle = (throttle - (0.4 * dt))
    else
    end
    if (throttle < 0) then
    throttle = 0
    else
    end
    if (throttle > 1) then
    throttle = 1
    else
    end
    speed = math.sqrt((((vx * vx) + (vy * vy)) + (vz * vz)))
    vx = (vx + (((entity.transform:forward().x * 32) * throttle) * dt))
    vy = (vy + (((entity.transform:forward().y * 32) * throttle) * dt))
    vz = (vz + (((entity.transform:forward().z * 32) * throttle) * dt))
    vy = (vy - (13 * dt))
    d = ((0.02 * speed) * dt)
    vx = (vx - (vx * d))
    vy = (vy - (vy * d))
    vz = (vz - (vz * d))
    speed = math.sqrt((((vx * vx) + (vy * vy)) + (vz * vz)))
    a = (1 - math.exp((0 - ((speed * 0.1) * dt))))
    vx = (vx + (((entity.transform:forward().x * speed) - vx) * a))
    vy = (vy + (((entity.transform:forward().y * speed) - vy) * a))
    vz = (vz + (((entity.transform:forward().z * speed) - vz) * a))
    entity.transform.position.x = (entity.transform.position.x + (vx * dt))
    entity.transform.position.y = (entity.transform.position.y + (vy * dt))
    entity.transform.position.z = (entity.transform.position.z + (vz * dt))
    hud.set("throttle", throttle)
    hud.set("speed", speed)
end

