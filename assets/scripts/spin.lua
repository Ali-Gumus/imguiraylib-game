-- spin.lua — rotates its entity. The engine calls on_update(entity, dt)
-- every frame; `entity` is the real C++ entity, edits apply instantly.

local speed = 90            -- degrees per second
local t = 0                 -- our own clock (the os library is sandboxed away)

-- All three hooks are optional; define the ones you need.
function on_start(entity)
    print("[" .. entity.name .. "] on_start")
end

function on_destroy(entity)
    print("[" .. entity.name .. "] on_destroy")
end

function on_update(entity, dt)
    t = t + dt
    -- rotate around the local Y axis; quaternion-safe (no gimbal lock)
    entity.transform:rotate(0, 1, 0, speed * dt)

    -- try uncommenting: bob up and down
    -- entity.transform.position.y = 0.5 + 0.5 * math.sin(t * 2)
end
