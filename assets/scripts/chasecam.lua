-- chasecam.lua — smooth chase camera. Put on a Camera entity that is
-- NOT parented (this script drives its world position/orientation).
-- Follows the entity named below from behind and above, with lag.

local target_name = "Jet"
local distance   = 9      -- units behind the jet
local height     = 3      -- units above the jet
local stiffness  = 4      -- higher = snappier (units of 1/second)

function on_update(entity, dt)
    local jet = scene.find(target_name)
    if jet == nil then return end
    local jt = jet.transform
    local f  = jt:forward()        -- jet's facing (world unit vector)

    -- Desired camera spot: behind the jet (-forward) and a bit above.
    local dx = jt.position.x - f.x * distance
    local dy = jt.position.y - f.y * distance + height
    local dz = jt.position.z - f.z * distance

    -- Smooth follow: close a fraction of the gap each frame. Using
    -- 1 - exp(-k*dt) makes the smoothing frame-rate independent.
    local a = 1 - math.exp(-stiffness * dt)
    local t = entity.transform
    local px, py, pz = t.position.x, t.position.y, t.position.z
    t.position.x = px + (dx - px) * a
    t.position.y = py + (dy - py) * a
    t.position.z = pz + (dz - pz) * a

    -- Always aim at the jet (world up keeps the view from rolling).
    t:look_at(jt.position.x, jt.position.y, jt.position.z)
end
