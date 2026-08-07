-- enemy.lua
-- =============================================================================
-- Flying-enemy AI that behaves like an aircraft, not a tracking drone. It
-- ALWAYS moves forward at a constant speed and can only TURN so fast, so when
-- it can't turn quickly enough it overshoots the player and loops back around
-- -- the feel of a dogfight. It steers away from other enemies so a squadron
-- doesn't merge, climbs over the landscape rather than through it, and fires
-- at where the player is GOING rather than where it is.
--
-- Attach to an enemy entity (tag "enemy", plus a Health component). It hunts
-- the nearest entity tagged "player".
--
-- WHY IT AIMS AHEAD OF THE TARGET. A round takes time to arrive, and the thing
-- it is aimed at does not wait for it. At 1200 metres this round is in the air
-- for about 1.3 seconds, during which a jet at cruise covers some 320 metres --
-- more than twenty times its own length. Firing at where the aircraft IS
-- therefore cannot land a long shot at all: the round passes through empty air
-- well behind it every single time, and the enemy reads as harmless rather than
-- as missing. The aiming point has to be where the target WILL BE.
--
-- Working that out exactly is a quadratic, because the round's flight time
-- depends on how far away the aiming point is while the aiming point depends on
-- the flight time. Rather than solve it algebraically this guesses a flight time
-- from the present distance, moves the target along by that much, then
-- re-measures against the moved point and repeats. Two passes land within a few
-- metres at any speed this game reaches, and it stays correct if the round is
-- retuned -- which a closed-form solution copied from somewhere would not.
--
-- The same point is used to STEER by, not only to shoot at. Turning toward
-- where the target is going, rather than toward the target, is the difference
-- between chasing something and cutting it off; it is also what puts the nose on
-- the firing solution so the shot can actually be taken.
--
-- WHY IT WATCHES THE GROUND. The landscape has peaks over seven hundred metres
-- and an enemy is flown by setting its transform directly, so nothing stops it
-- occupying the same space as a hillside -- it simply passes through, which
-- looks like the terrain is scenery rather than a place. Two mechanisms handle
-- it: the aircraft looks along its own path and climbs when the ground ahead
-- rises into it, and a hard floor catches the cases the turn rate could not.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
--
-- Sized and paced as a real Mi-24 attack helicopter, the model these enemies
-- wear, at the engine's one-unit-is-one-metre scale. It is 17.5 metres long and
-- tops out near 93 metres per second, which makes it far slower than the
-- player's jet - so an enemy cannot chase the player down, and a dogfight is
-- about the player choosing to come back around rather than about the two
-- matching speeds.
--
-- Two of these numbers are not free choices:
--
--   muzzle    Must reach past the helicopter's own collision shape, which is
--             about 18 metres long and so extends 9 metres forward of its
--             middle. A round spawned inside its own shooter is solid against
--             it and gets shoved aside instead of flying, which is why this is
--             not simply "just in front of the nose".
--
--   sep_range Separation is what stops a squadron converging into one point.
--             It has to be measured against how big these are: two 18-metre
--             aircraft holding 8 metres apart are overlapping, so this scales
--             with the airframe, not with the old placeholder cubes.
--
--   hit_*     The shape bullets test against. These are not chosen by feel:
--             they are the helicopter's real proportions. Measuring the model
--             file gives a mesh 1675 units across the rotor disc, which at the
--             0.0103 scale in models.lua is the Mi-24's true 17.3-metre rotor
--             and a 19.8-metre overall length. A capsule of radius 2.95 and
--             body length 12.75 spans 18.65 metres nose to tail (the two
--             hemisphere ends add a radius each), which covers the airframe
--             without claiming the empty air the spinning rotor sweeps.
properties = {
    speed       = 80,    -- constant forward flight speed (metres/sec)
    turn_rate   = 30,    -- most degrees per second it can rotate toward the target
    fire_range  = 1200,  -- only shoot when the player is at least this close
    fire_angle  = 12,    -- only shoot when the nose is within this many degrees
    fire_rate   = 0.5,   -- seconds between shots
    muzzle      = 12.0,  -- spawn bullets this far ahead; must clear the collider
    sep_range   = 60,    -- start avoiding other enemies within this distance
    sep_force   = 40,    -- how strongly to push apart from a crowding neighbor
    points      = 1,     -- score awarded to the player when this enemy dies
    hit_radius  = 2.95,  -- half the width of its hittable capsule (metres)
    hit_length  = 12.75, -- the capsule's straight section, ends added on top

    -- --- Leading the shot ----------------------------------------------------
    -- How fast the round leaves the muzzle, in metres per second.
    --
    -- This MUST match the `speed` property of enemy_bullet.lua. The lead is
    -- worked out from this number, so if the round actually travels at a
    -- different speed then every shot misses by the difference -- and it misses
    -- silently, since nothing anywhere can notice that two scripts disagree. It
    -- is repeated here rather than read from there because a script cannot see
    -- another script's properties.
    shell_speed = 860,

    -- --- Staying out of the landscape ----------------------------------------
    -- How high above the ground it tries to stay, in metres. This is a
    -- CLEARANCE, not a cruising height: the aircraft is free to fly at any
    -- altitude it likes and only starts climbing when the ground comes up to
    -- within this much of it. Raising it makes enemies hug the terrain less and
    -- follow a diving player less far down.
    min_agl = 120,

    -- How far ahead it looks, expressed in SECONDS of its own flight rather than
    -- in metres. An aircraft needs enough warning to turn, and how much ground
    -- it covers while turning depends on how fast it is going -- so a distance
    -- written in metres would quietly become too short the moment `speed` was
    -- raised. Four seconds at 80 m/s looks about 320 metres ahead, which is
    -- comfortably more than this airframe's turning circle.
    look_ahead_time = 4.0,

    -- How many points along that path are checked. See highestGroundAhead below
    -- for why one is not enough.
    ground_samples = 4,

    -- How far ABOVE the required clearance it aims while climbing, in metres.
    -- Steering only asks for a direction and the turn rate means the nose takes
    -- time to get there, so aiming at exactly the height it needs arrives at
    -- that height late. Overshooting the request is what makes it arrive on
    -- time; the aircraft levels off by itself as soon as the ground stops
    -- rising.
    climb_margin = 80,

    -- The hard floor, in metres above the ground directly underneath.
    --
    -- Steering can fail: an aircraft that comes over a ridge already committed
    -- cannot always pull up in time whatever it wants, and separation from a
    -- crowding neighbour can shove one downward. This catches those cases. It
    -- should almost never do anything -- if enemies are visibly skating along
    -- the hillsides, the answer is a larger `min_agl` or `look_ahead_time`,
    -- not a larger floor.
    floor_agl = 25,
}

function onStart(entity)
    -- Ensure this enemy has a hitbox so bullets register on the whole body.
    -- This only sets a default for spawned enemies (which start with none); an
    -- enemy given a Collider component in the editor keeps that authored shape.
    --
    -- A CAPSULE rather than a ball, because the shape has to resemble the
    -- aircraft. A helicopter is roughly six metres across and twenty long, so
    -- the ball that encloses it is twenty metres wide in every direction and
    -- three quarters of it is empty sky: rounds passing well clear of the
    -- fuselage count as hits. The capsule follows the body instead, and is
    -- laid nose to tail automatically.
    Scene.setCollider(entity, "capsule", properties.hit_radius,
                      properties.hit_length)

    -- And a rigid body, or the physics simulation does not know it exists.
    -- Bullets are physical objects now and report their hits through
    -- onCollision, which only fires between bodies the simulation owns - so
    -- without this a spawned enemy is completely bulletproof. KINEMATIC because
    -- this script flies the enemy by setting its transform directly; it still
    -- registers contacts with the dynamic bullets. Add-only-if-missing, so an
    -- enemy set up in the editor keeps whatever was chosen there.
    Physics.setBody(entity, "kinematic")
end

local cooldown = 0       -- seconds until the enemy can fire again (runtime state)

-- The player's velocity, smoothed. Read from the engine rather than worked out
-- here: `entity.velocity` is sampled at one fixed point in every frame, so it
-- does not depend on where in the component list this script happens to sit and
-- does not wobble with the frame rate. Measuring it locally as a change in
-- position over dt divides one frame's movement by another frame's duration,
-- and at 300 m/s a frame 15% out of step misreads the speed by 15% -- which for
-- something aiming at where the target WILL BE is a lead error of tens of
-- metres.
local vx, vy, vz = 0, 0, 0
local has_vel = false

-- The highest the ground gets along the path ahead, in world metres.
--
-- WHY SEVERAL SAMPLES RATHER THAN ONE. Asking only about the point the aircraft
-- will reach in a few seconds says nothing about what stands between here and
-- there. A ridge with a valley behind it answers "the ground is low" at exactly
-- the moment the ridge is the problem, so the aircraft holds its height and
-- flies into the near face. Walking outward and keeping the WORST answer turns
-- the question from "how high is that one spot" into "how high does anything get
-- on the way", which is the question an aircraft actually needs answered.
--
-- The direction is given as a flat heading rather than as the nose vector,
-- because the ground it will cross is decided by where it is going over the MAP.
-- An aircraft in a steep climb still crosses the same ridge; sampling along the
-- raw nose would look at a strip of map far shorter than the one it will travel.
--
-- Samples start one step out rather than at zero, because the ground directly
-- underneath is a separate question and is asked separately below.
local function highestGroundAhead(t, hx, hz, distance, samples)
    local p = t.position
    local worst = -100000
    for i = 1, samples do
        local along = distance * (i / samples)
        local h = Scene.groundHeight(p.x + hx * along, p.z + hz * along)
        if h > worst then worst = h end
    end
    return worst
end

function onUpdate(entity, dt)
    local t = entity.transform
    local P = properties

    cooldown = cooldown - dt

    -- Find the player (nearest entity tagged "player"; a huge radius just means
    -- "wherever it is"). Idle if there is no player.
    local target = Scene.nearest("player", t.position.x, t.position.y, t.position.z, 100000)
    if target == nil then
        has_vel = false   -- forget the old reading, or a respawn reads as a jump
        return
    end
    local tp = target.transform.position

    -- --- How fast the player is going ----------------------------------------
    local pv = target.velocity
    if not has_vel then
        -- Nothing has been smoothed yet, so the first reading is taken whole.
        -- Easing up from a standing start would mean aiming, on the first frame
        -- of an engagement, at where a STATIONARY aircraft would be -- which is
        -- just where it already is, the very thing the lead exists to avoid.
        vx, vy, vz = pv.x, pv.y, pv.z
        has_vel = true
    else
        -- The engine's figure is measured over a single frame, and a manoeuvring
        -- target changes it every frame. An aircraft that chased each fresh
        -- reading would swing its nose about and never settle inside its own
        -- firing cone. Smoothing also means the lead follows what the target has
        -- BEEN doing rather than one instant of it, which is the right thing to
        -- extrapolate from.
        local a = 1 - math.exp(-12 * dt)
        vx = vx + (pv.x - vx) * a
        vy = vy + (pv.y - vy) * a
        vz = vz + (pv.z - vz) * a
    end

    -- --- Where the player will be when a round gets there ---------------------
    -- The round leaves the muzzle at `shell_speed` measured against THIS
    -- aircraft, and the engine then adds this aircraft's own motion on top,
    -- because a muzzle velocity is relative to the gun and not to the ground.
    -- Over the ground it therefore travels at the sum of the two. Leading on the
    -- muzzle figure alone would over-estimate the flight time by about a tenth
    -- and place every shot that much too far in front of the target.
    local shell = P.shell_speed + P.speed

    local aimx, aimy, aimz = tp.x, tp.y, tp.z
    for _ = 1, 2 do
        local ax = aimx - t.position.x
        local ay = aimy - t.position.y
        local az = aimz - t.position.z
        local flight = math.sqrt(ax * ax + ay * ay + az * az) / shell
        aimx = tp.x + vx * flight
        aimy = tp.y + vy * flight
        aimz = tp.z + vz * flight
    end

    -- --- Is the landscape in the way? ----------------------------------------
    -- The heading over the ground, as a flat unit vector. The nose's vertical
    -- component is dropped for the reason given on highestGroundAhead.
    local nose = t:forward()
    local hlen = math.sqrt(nose.x * nose.x + nose.z * nose.z)
    local avoiding = false

    -- Pointing more or less straight up or down leaves no heading to speak of,
    -- and normalising a vector of length zero produces nonsense. Such an
    -- aircraft is also not about to fly into anything horizontally, so the
    -- look-ahead is simply skipped and the floor below is left to cover it.
    if hlen > 0.001 then
        local hx, hz = nose.x / hlen, nose.z / hlen
        local ahead   = P.speed * P.look_ahead_time
        local samples = math.max(1, math.floor(P.ground_samples))
        local needed  = highestGroundAhead(t, hx, hz, ahead, samples) + P.min_agl

        if t.position.y < needed then
            -- Climb over it. The aiming point is placed at the far end of the
            -- look-ahead, on the CURRENT heading, at the height that clears the
            -- obstacle -- which asks for exactly the climb angle needed to top
            -- it, and no more.
            --
            -- Keeping the present heading rather than the target's bearing is
            -- deliberate. The aircraft was already turning toward the player, so
            -- the heading is roughly right; steering at a lead point that lies
            -- kilometres away while trying to gain a few hundred metres would
            -- ask for a climb so shallow that the ridge arrives first.
            aimx = t.position.x + hx * ahead
            aimy = needed + P.climb_margin
            aimz = t.position.z + hz * ahead
            avoiding = true
        end
    end

    -- Turn toward the aiming point, but no faster than turn_rate. One call,
    -- whether that point is a firing solution or a way over a hill.
    t:rotateToward(aimx, aimy, aimz, P.turn_rate * dt)

    -- Always keep flying forward along the nose.
    t:translateLocal(0, 0, -P.speed * dt)

    -- Separation: push away from the nearest OTHER enemy if too close, so the
    -- squadron spreads around the player instead of stacking up.
    local other = Scene.nearestOther(entity, "enemy", P.sep_range)
    if other ~= nil then
        local op = other.transform.position
        local sx = t.position.x - op.x
        local sy = t.position.y - op.y
        local sz = t.position.z - op.z
        local d  = math.sqrt(sx * sx + sy * sy + sz * sz)
        if d > 0.001 then
            local strength = (P.sep_range - d) / P.sep_range
            local push = P.sep_force * strength * dt / d
            t.position.x = t.position.x + sx * push
            t.position.y = t.position.y + sy * push
            t.position.z = t.position.z + sz * push
        end
    end

    -- --- The floor ------------------------------------------------------------
    -- The last resort that makes flying through a hillside impossible rather
    -- than merely unlikely. Everything above is STEERING, and steering has a
    -- turn rate: an aircraft that crests a ridge already committed cannot always
    -- pull up in time, and the separation push just applied can move it downward
    -- as well. Because the aircraft is kinematic its height is simply whatever
    -- this script says it is, so the ground can be enforced directly.
    --
    -- Sampled underneath its position AFTER the frame's movement, so the answer
    -- is about where it now is rather than where it was.
    local floor = Scene.groundHeight(t.position.x, t.position.z) + P.floor_agl
    if t.position.y < floor then t.position.y = floor end

    -- --- Fire, if there is a shot --------------------------------------------
    -- An aircraft climbing to clear a hill is pointing at the hill, not at the
    -- target, so its nose lines up with nothing worth shooting. Checking this
    -- first also stops it hosing rounds into a hillside on the way up.
    if avoiding then return end
    if cooldown > 0 then return end

    -- Range is measured to where the player actually IS. The lead decides where
    -- to point; whether the target is close enough to engage at all is a fact
    -- about the real aircraft, and a fast crossing target's lead point can sit
    -- hundreds of metres outside a range it is comfortably inside.
    local dx = tp.x - t.position.x
    local dy = tp.y - t.position.y
    local dz = tp.z - t.position.z
    if math.sqrt(dx * dx + dy * dy + dz * dz) > P.fire_range then return end

    -- How far off the nose the AIMING POINT is -- the firing solution, not the
    -- target. Lining up on the target itself would mean firing at the moment the
    -- nose is pointing exactly where the round must not go.
    --
    -- The nose is read again here rather than reused from the terrain check
    -- above, because the aircraft has turned and moved since then. Testing the
    -- alignment of a nose that is a frame out of date, and then launching a
    -- round along the current one, aims at neither.
    local f = t:forward()
    local ex = aimx - t.position.x
    local ey = aimy - t.position.y
    local ez = aimz - t.position.z
    local elen = math.sqrt(ex * ex + ey * ey + ez * ez)
    if elen < 0.001 then return end

    local dot = (f.x * ex + f.y * ey + f.z * ez) / elen
    if dot >  1 then dot =  1 end
    if dot < -1 then dot = -1 end
    if math.deg(math.acos(dot)) > P.fire_angle then return end

    cooldown = P.fire_rate

    -- The last three arguments are this helicopter's own velocity, which the
    -- engine adds to the round's muzzle velocity after enemy_bullet.lua has
    -- launched it - a gun's muzzle speed is measured against the gun, not
    -- against the ground. It needs no frame-to-frame measurement here: this
    -- script flies the enemy by pushing it along its nose at a constant
    -- speed, so forward x speed IS its velocity, exactly. It is also the
    -- assumption the lead above is built on, which is why the two must agree.
    --
    -- The three nils before them are the tag, health and model a bullet does
    -- not want. They are in the way because the velocity was added to
    -- Scene.spawn last, after those three already existed.
    Scene.spawn("EnemyBullet",
        t.position.x + f.x * P.muzzle, t.position.y + f.y * P.muzzle, t.position.z + f.z * P.muzzle,
        f.x, f.y, f.z,
        "assets/scripts/enemy_bullet.lua", nil, nil, nil,
        f.x * P.speed, f.y * P.speed, f.z * P.speed)
end

-- Runs when this enemy is destroyed (its health reached zero). The enemy grants
-- its own point value, so scoring stays a property of the target rather than of
-- whatever weapon killed it.
function onDestroy(entity)
    Hud.add("score", properties.points)

    -- Blow up where the enemy was standing. This runs for any death, so an
    -- enemy that crashes explodes exactly like one that was shot down.
    local p = entity.transform.position
    Fx.burst("explosion", p.x, p.y, p.z)
    -- Heard from where the enemy died. A kill across the valley should sound
    -- distant and off to one side, not like it happened in the cockpit.
    Audio.playAt("explosion", p.x, p.y, p.z)
end
