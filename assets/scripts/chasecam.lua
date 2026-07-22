-- chasecam.lua
-- =============================================================================
-- A follow ("chase") camera. Attach it to a Camera entity that has NO parent,
-- because this script sets the camera's world position and orientation itself.
-- Each frame it moves toward a point behind and above a target entity, easing
-- in smoothly rather than snapping, and always turns to look at that target.
-- =============================================================================

local target_name = "Jet"   -- the entity to follow, matched by its name
local distance    = 9       -- how far behind the target to sit (world units)
local height      = 3       -- how far above the target to sit (world units)
local stiffness   = 4       -- follow tightness; higher snaps faster, lower drifts

function on_update(entity, dt)
    -- Look up the entity to follow by name. Returns nil if it does not exist
    -- (for example before it is created), in which case we do nothing this frame.
    local jet = scene.find(target_name)
    if jet == nil then return end

    local jt = jet.transform     -- the target's position + orientation
    local f  = jt:forward()      -- unit vector out of the target's nose (world space)

    -- Work out the ideal camera spot: start at the target, step BACKWARDS along
    -- its facing (subtracting forward * distance), then lift straight up by height.
    local dx = jt.position.x - f.x * distance
    local dy = jt.position.y - f.y * distance + height
    local dz = jt.position.z - f.z * distance

    -- Smooth follow. Instead of jumping to the ideal spot, close only a FRACTION
    -- of the remaining gap each frame. The fraction 1 - exp(-stiffness*dt) makes
    -- the smoothing behave the same regardless of frame rate.
    local a = 1 - math.exp(-stiffness * dt)

    local t = entity.transform                       -- the camera's transform
    local px, py, pz = t.position.x, t.position.y, t.position.z
    t.position.x = px + (dx - px) * a                -- move part-way toward dx
    t.position.y = py + (dy - py) * a                -- move part-way toward dy
    t.position.z = pz + (dz - pz) * a                -- move part-way toward dz

    -- Rotate the camera so its forward points at the target. look_at keeps the
    -- camera upright (world up), so the view does not tilt as the target rolls.
    t:look_at(jt.position.x, jt.position.y, jt.position.z)
end
