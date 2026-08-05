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

    -- --- SPEED FEEL: the field of view opens as the aircraft goes faster -----
    --
    -- WHY THIS IS NEEDED AT ALL. A sense of speed does not come from metres per
    -- second, it comes from how fast things sweep ACROSS the view - and that is
    -- speed divided by how far away they are. Over a landscape whose smallest
    -- hill is hundreds of metres across, seen from a kilometre up, the ground
    -- below turns under the aircraft at about fifteen degrees a second however
    -- fast it is really going. The number on the HUD climbs and the picture
    -- barely changes.
    --
    -- Widening the field of view fixes that without touching the world. A wider
    -- lens sweeps more of the scene past the edges of the screen for the same
    -- motion, and the edges are where peripheral vision reads self-motion from.
    -- It is what every flight and racing game does, and it works because it is
    -- aimed at the same perceptual channel the problem is in.
    --
    -- The cost, worth knowing before tuning it wide: a wider view also makes
    -- everything in it SMALLER, so the aircraft appears to shrink as it
    -- accelerates and distant things get harder to pick out. Past about 90 the
    -- edges of the picture also start to look stretched, which is projection
    -- doing exactly what it should rather than a bug.
    --
    -- Set fov_fast equal to fov_slow to switch the whole effect off.
    fov_slow = 60,    -- degrees, at or below fov_slow_speed
    fov_fast = 85,    -- degrees, at or above fov_fast_speed

    -- The speeds those two are measured at, in metres per second. The lower one
    -- is around a fast cruise, so ordinary flying looks normal and only genuinely
    -- going somewhere widens it; the upper is the F-16's top speed, so the whole
    -- range is used rather than saturating early.
    fov_slow_speed = 200,
    fov_fast_speed = 685,

    -- How quickly the view opens and closes, in the same units as `stiffness`.
    -- Deliberately slower than the camera swing: a field of view that tracked
    -- speed exactly would twitch on every throttle change and every gust, and
    -- the effect works better when it is felt rather than noticed.
    fov_ease = 1.5,
}

-- The target's speed, measured here rather than read from anywhere.
--
-- Nothing publishes a velocity that this can rely on, and measuring it is three
-- lines - the same choice hud.lua makes, for the same reason: it then works for
-- ANY target however it is being moved, whether by the JSBSim flight model, by
-- flight_sim.lua, or by a script that has not been written yet.
local lx, ly, lz = nil, nil, nil   -- where the target was last frame
local speed      = 0               -- metres per second
local fov        = nil             -- the eased field of view, degrees

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

function onUpdate(entity, dt)
    local P = properties

    -- Find the target by name; do nothing if it doesn't exist yet.
    local jet = Scene.find(target_name)
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
    -- the position. Plain lookAt would force world up here and throw the roll
    -- away, leaving the camera upright however far the aircraft is over.
    t:lookAtUp(jt.position.x, jt.position.y, jt.position.z, ux, uy, uz)

    -- --- Open the view with speed -------------------------------------------
    -- Measure how far the target moved since last frame. Guarding against a
    -- zero dt matters: a paused or first frame would divide by nothing and
    -- produce an infinite speed, which would peg the view wide open for good.
    local jp = jt.position
    if lx ~= nil and dt > 0 then
        local dx, dy, dz = jp.x - lx, jp.y - ly, jp.z - lz
        speed = math.sqrt(dx * dx + dy * dy + dz * dz) / dt
    end
    lx, ly, lz = jp.x, jp.y, jp.z

    local cam = entity:getComponent_Camera()
    if cam == nil then return end

    -- Where this speed falls between the two reference speeds, as 0 to 1.
    local span = P.fov_fast_speed - P.fov_slow_speed
    local k = 0
    if span > 0 then k = (speed - P.fov_slow_speed) / span end
    if k < 0 then k = 0 elseif k > 1 then k = 1 end
    local want = P.fov_slow + (P.fov_fast - P.fov_slow) * k

    -- Ease toward it, by the same frame-rate-independent fraction the camera
    -- offset uses, so the view opens smoothly instead of tracking every wobble.
    if fov == nil then fov = want end
    fov = fov + (want - fov) * (1 - math.exp(-P.fov_ease * dt))
    cam.fovy = fov
end
