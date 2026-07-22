-- chasecam.lua
-- =============================================================================
-- A follow ("chase") camera. Attach it to a Camera entity that has NO parent,
-- because this script sets the camera's world position and orientation itself.
-- Each frame it eases toward a point behind and above a target entity and
-- turns to look at that target.
-- =============================================================================

-- The entity to follow, matched by name. This is text, not a number, so it
-- stays a plain local (the Inspector's property fields are numbers only).
local target_name = "Jet"

-- Tunable numbers, shown as editable fields in the Inspector.
properties = {
    distance  = 9,   -- how far behind the target to sit (world units)
    height    = 3,   -- how far above the target to sit (world units)
    stiffness = 4,   -- follow tightness; higher snaps faster, lower drifts
}

function on_update(entity, dt)
    local P = properties

    -- Find the target by name; do nothing if it doesn't exist yet.
    local jet = scene.find(target_name)
    if jet == nil then return end

    local jt = jet.transform
    local f  = jt:forward()

    -- Ideal camera spot: behind the target (minus forward) and above it.
    local dx = jt.position.x - f.x * P.distance
    local dy = jt.position.y - f.y * P.distance + P.height
    local dz = jt.position.z - f.z * P.distance

    -- Ease toward that spot: close a fraction of the gap each frame. The
    -- fraction 1 - exp(-stiffness*dt) behaves the same at any frame rate.
    local a = 1 - math.exp(-P.stiffness * dt)

    local t = entity.transform
    local px, py, pz = t.position.x, t.position.y, t.position.z
    t.position.x = px + (dx - px) * a
    t.position.y = py + (dy - py) * a
    t.position.z = pz + (dz - pz) * a

    -- Look at the target (world up keeps the view from rolling).
    t:look_at(jt.position.x, jt.position.y, jt.position.z)
end
