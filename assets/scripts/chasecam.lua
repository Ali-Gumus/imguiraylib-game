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
    distance  = 22,  -- how far behind the target to sit (world units)
    height    = 6,   -- how far above the target to sit (world units)
    stiffness = 6,   -- how fast the camera swings round when the target turns
}

-- The camera's current offset FROM the target, in world space. This is the state
-- that gets smoothed, and the reason is worth understanding.
--
-- THE TRAP: easing the camera's world POSITION toward a point attached to a
-- moving target does not settle where you asked. Exponential smoothing closes a
-- fixed fraction of the gap each second, so while the target keeps moving the
-- camera settles at a standing distance behind of roughly
--
--     target speed / stiffness
--
-- which for a fast aircraft swamps the distance setting entirely: at 685 metres
-- per second with a stiffness of 4 the camera trails about 170 metres back, and
-- no value of `distance` changes that. The faster the aircraft, the further away
-- it looks - and because the error grows with speed, no single stiffness can fix
-- it either. Stiff enough for top speed is rigid at landing speed.
--
-- THE FIX: smooth the OFFSET rather than the position, then add the target's
-- position afterwards. Translation is then exact at any speed - the camera is
-- always precisely `distance` behind - and the smoothing does the job it is
-- actually wanted for, which is easing the swing when the aircraft ROTATES.
local ox, oy, oz = nil, nil, nil

function on_update(entity, dt)
    local P = properties

    -- Find the target by name; do nothing if it doesn't exist yet.
    local jet = scene.find(target_name)
    if jet == nil then return end

    local jt = jet.transform
    local f  = jt:forward()

    -- Where the camera wants to sit relative to the target: behind it (against
    -- the nose direction) and above it.
    local wx = -f.x * P.distance
    local wy = -f.y * P.distance + P.height
    local wz = -f.z * P.distance

    -- On the very first frame there is no previous offset to ease from. Snapping
    -- straight to the wanted one avoids the camera sweeping in from wherever it
    -- happened to be left in the editor.
    if ox == nil then ox, oy, oz = wx, wy, wz end

    -- Ease the offset toward the wanted one. The fraction 1 - exp(-stiffness*dt)
    -- closes the same proportion per SECOND whatever the frame rate, so the feel
    -- does not change with performance.
    local a = 1 - math.exp(-P.stiffness * dt)
    ox = ox + (wx - ox) * a
    oy = oy + (wy - oy) * a
    oz = oz + (wz - oz) * a

    -- Position is the target plus the smoothed offset, so however fast the
    -- target is travelling the camera keeps pace with it exactly.
    local t = entity.transform
    t.position.x = jt.position.x + ox
    t.position.y = jt.position.y + oy
    t.position.z = jt.position.z + oz

    -- Look at the target (world up keeps the view from rolling).
    t:look_at(jt.position.x, jt.position.y, jt.position.z)
end
