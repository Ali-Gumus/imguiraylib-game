-- spin.lua
-- =============================================================================
-- Rotates its entity around the local Y axis at a steady rate. A minimal
-- example of a per-frame script.
-- =============================================================================

-- Tunable value, shown as an editable field in the Inspector.
properties = {
    speed = 90,   -- degrees per second
}

function on_update(entity, dt)
    -- rotate around the local Y axis; quaternion-safe (no gimbal lock)
    entity.transform:rotate(0, 1, 0, properties.speed * dt)
end
