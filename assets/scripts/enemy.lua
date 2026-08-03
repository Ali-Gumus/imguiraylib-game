-- enemy.lua
-- =============================================================================
-- Flying-enemy AI that behaves like an aircraft, not a tracking drone. It
-- ALWAYS moves forward at a constant speed and can only TURN so fast, so when
-- it can't turn quickly enough it overshoots the player and loops back around
-- -- the feel of a dogfight. It fires only when its nose is lined up on the
-- player, and steers away from other enemies so a squadron doesn't merge.
--
-- Attach to an enemy entity (tag "enemy", plus a Health component). It hunts
-- the nearest entity tagged "player".
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

function onUpdate(entity, dt)
    local t = entity.transform
    local P = properties

    -- Find the player (nearest entity tagged "player"; a huge radius just means
    -- "wherever it is"). Idle if there is no player.
    local target = Scene.nearest("player", t.position.x, t.position.y, t.position.z, 100000)
    if target == nil then return end
    local tp = target.transform.position

    -- Turn toward the player, but no faster than turn_rate.
    t:rotateToward(tp.x, tp.y, tp.z, P.turn_rate * dt)

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

    -- Distance to the player, and how far off the nose it is.
    local dx = tp.x - t.position.x
    local dy = tp.y - t.position.y
    local dz = tp.z - t.position.z
    local dist = math.sqrt(dx * dx + dy * dy + dz * dz)

    local f = t:forward()
    local inv = (dist > 0.0001) and (1.0 / dist) or 0.0
    local dot = f.x * dx * inv + f.y * dy * inv + f.z * dz * inv
    if dot >  1 then dot =  1 end
    if dot < -1 then dot = -1 end
    local aim_angle = math.deg(math.acos(dot))

    -- Fire when in range, roughly in front, and the gun is ready.
    cooldown = cooldown - dt
    if dist < P.fire_range and aim_angle < P.fire_angle and cooldown <= 0 then
        cooldown = P.fire_rate
        -- The last three arguments are this helicopter's own velocity, which the
        -- engine adds to the round's muzzle velocity after enemy_bullet.lua has
        -- launched it - a gun's muzzle speed is measured against the gun, not
        -- against the ground. It needs no frame-to-frame measurement here: this
        -- script flies the enemy by pushing it along its nose at a constant
        -- speed, so forward x speed IS its velocity, exactly.
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
