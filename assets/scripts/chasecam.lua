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
    -- How much of the target's ROLL the camera copies, from 0 to 1.
    --
    -- 0 keeps the horizon level however hard the aircraft banks: the camera
    -- watches from outside, and a roll looks like the aircraft turning over in
    -- front of you. 1 mounts the camera rigidly to the airframe, so a roll
    -- spins the whole world instead - the view from behind the canopy.
    --
    -- Part-way is what most flight games actually use. Full coupling is
    -- convincing but can be uncomfortable during a fast roll, while none at all
    -- makes an aircraft feel like it is being watched rather than flown. 0.7
    -- tilts the horizon clearly while leaving some of the world's own upright.
    --
    -- BUT the in-between values are not a clean fraction of the bank angle, and
    -- it is worth knowing where that shows. The blend mixes the two up
    -- DIRECTIONS and then re-normalises, which is not the same as taking a
    -- fraction of the angle between them: at 0.7 a 45 degree bank tilts the
    -- horizon about 32 degrees (close to the 31 you would expect), a 90 degree
    -- bank tilts about 67 (more than 63), and past about 120 it runs ahead
    -- faster still until, once fully inverted, ANY setting above 0.5 flips the
    -- horizon the whole way over. The two vectors are opposite there, so what
    -- they average to is decided by the smaller share.
    --
    -- For ordinary banking turns this is invisible and feels right. It only
    -- matters for aerobatics, and the honest fix would be to rotate world up
    -- about the aircraft's forward axis by roll x bank_angle instead of blending
    -- the vectors. 0 and 1 are both exact whatever the manoeuvre.
    --
    -- It also decides where the camera SITS, not just how it is turned: at 1 the
    -- height is measured along the aircraft's own up, so the camera stays over
    -- the canopy even inverted, while at 0 it is measured straight up in the
    -- world. Anything between blends the two, so the position and the horizon
    -- always agree with each other.
    roll      = 0.7,
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

    -- The "up" this camera works in: world up blended toward the AIRCRAFT's own
    -- up by the roll setting. One direction serves both jobs below - where the
    -- camera sits and which way its horizon lies - so the two can never
    -- disagree, and roll = 0 reduces exactly to plain world up.
    local ju = jt:up()
    local r  = P.roll
    if r < 0 then r = 0 elseif r > 1 then r = 1 end
    local ux = ju.x * r
    local uy = ju.y * r + (1 - r)
    local uz = ju.z * r
    -- Blending two unit vectors shortens the result (they do not point the same
    -- way), so it has to be re-normalised or `height` would shrink as the
    -- aircraft banks.
    local ul = math.sqrt(ux * ux + uy * uy + uz * uz)
    if ul > 0.0001 then
        ux, uy, uz = ux / ul, uy / ul, uz / ul
    else
        -- The blend cancelled out, which needs the aircraft inverted at exactly
        -- roll = 0.5. Falling back to world up keeps the frame sane.
        ux, uy, uz = 0, 1, 0
    end

    -- Where the camera wants to sit relative to the target: behind it (against
    -- the nose direction) and above it, "above" being the blended up.
    local wx = -f.x * P.distance + ux * P.height
    local wy = -f.y * P.distance + uy * P.height
    local wz = -f.z * P.distance + uz * P.height

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

    -- Look at the target, banking the horizon by the same blended up used for
    -- the position. Plain look_at would force world up here and throw the roll
    -- away, leaving the camera upright however far the aircraft is over.
    t:look_at_up(jt.position.x, jt.position.y, jt.position.z, ux, uy, uz)
end
