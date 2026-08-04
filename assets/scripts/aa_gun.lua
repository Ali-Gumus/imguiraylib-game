-- aa_gun.lua
-- =============================================================================
-- A ground anti-aircraft emplacement. It sits on the terrain, tracks the player,
-- LEADS the shot, and fires when its barrel is actually pointing where the
-- aircraft is going to be.
--
-- It is placed by aa_sites.lua, which puts clusters of these around the map, but
-- it works perfectly well dropped on an entity by hand.
--
-- WHY IT LEADS, AND WHY THAT IS THE WHOLE SCRIPT. The player's jet cruises at
-- 250 metres a second and tops out near 685. A shell leaving the barrel at 860
-- takes about 2.3 seconds to cross 2 kilometres, and in that time the aircraft
-- has moved between 570 metres and a mile and a half. Firing at where the jet IS
-- therefore misses by more than the length of an airfield, every single time.
-- Aiming has to be at where it WILL BE.
--
-- Solving that properly is a quadratic - the shell's flight time depends on how
-- far away the aiming point is, and the aiming point depends on the flight time.
-- Rather than solve it algebraically this guesses a flight time from the current
-- distance, moves the target along by that much, then re-measures and repeats.
-- Two rounds of that lands within a few metres at any speed this game reaches,
-- and it stays correct if the shell speed is retuned, which a closed-form
-- solution copied from somewhere would not.
--
-- IT IS TAGGED "aa", NOT "enemy", AND THAT MATTERS. gamemanager.lua starts the
-- next wave when Scene.count("enemy") reaches zero. Emplacements tagged "enemy"
-- would sit on the map permanently, that count would never reach zero, and the
-- game would silently stop spawning waves after the first one. The player's
-- bullets damage "aa" as well, so they can still be destroyed.
-- =============================================================================

properties = {
    -- How far out it will engage. Two kilometres is a realistic reach for a
    -- radar-directed gun and is far enough that the player meets fire before
    -- they can see what is shooting.
    range = 2000,

    -- Seconds between shots. Slower than the aircraft's own gun on purpose: a
    -- battery that fires as fast as a fighter is not a threat to fly past, it
    -- is a wall.
    fire_rate = 1.1,

    -- How fast the shell travels, which MUST match the `speed` property of
    -- enemy_bullet.lua - the lead is worked out from this number, and if the
    -- shell actually flies at a different speed every shot misses by the
    -- difference. It is repeated here rather than read from there because a
    -- script cannot see another script's properties.
    shell_speed = 860,

    -- How fast the mounting can slew, in degrees per second. This is what makes
    -- a fast, close pass survivable: the gun simply cannot follow, which is
    -- exactly how a real one behaves and why aircraft attack in a straight line
    -- rather than circling.
    turn_rate = 45,

    -- How close to lined up the barrel must be before it will fire. Firing
    -- while still slewing would put shells nowhere near the lead point.
    fire_angle = 8,

    -- It will not shoot at something on or below its own level - that would
    -- mean firing into the hillside it is standing on, and hitting its
    -- neighbours. 30 metres above the gun is enough to clear the emplacement.
    min_target_height = 30,

    -- How far in front of the mounting each shell appears. Must clear the gun's
    -- own collider, or the shell is born inside a solid object and is shoved
    -- aside instead of flying. See gun.lua for the same trap.
    muzzle = 14,

    -- The size of its hittable box, so the player can destroy it. Roughly a
    -- vehicle-sized emplacement.
    hit_radius = 6,

    -- HOW BIG IT IS DRAWN, in metres. This matters more than it sounds.
    --
    -- An entity spawned by Scene.spawn arrives as a ONE METRE cube. A vehicle
    -- the size of a dice, sitting three to nine kilometres away, is not small
    -- on screen - it is invisible, and it stays invisible while shooting at
    -- you, which reads as being shot at by nothing at all. The gun needs to be
    -- about the size of the vehicle it represents before it exists as far as
    -- the player is concerned.
    body_width  = 9,
    body_height = 5,
    body_depth  = 12,
}

-- Runtime state.
local cooldown = 0            -- seconds until it can fire again

-- The player's velocity, measured here rather than asked for. Nothing publishes
-- it, and measuring it is three lines - the same approach hud.lua takes, and it
-- means this works against any aircraft however it is being moved.
local px, py, pz = nil, nil, nil
local vx, vy, vz = 0, 0, 0

function onStart(entity)
    local P = properties

    -- Give it a body worth looking at - but ONLY when it is standing in as a
    -- plain cube.
    --
    -- The entity's own scale multiplies whatever it draws, INCLUDING a model,
    -- and a model already carries its real size from models.lua. Setting a
    -- 9 x 5 x 12 scale here would therefore stretch a vehicle model by those
    -- factors in three different directions - not a size mistake but a shear,
    -- and the sort that looks like a broken import rather than a wrong number.
    -- A model sizes itself; only the fallback cube needs telling.
    if not entity:hasComponent_Model() then
        local t = entity.transform
        t.scale.x, t.scale.y, t.scale.z = P.body_width, P.body_height, P.body_depth
    end

    -- Something for bullets to hit. Only added if missing, so an emplacement
    -- given a collider in the editor keeps the authored one.
    Scene.setCollider(entity, "sphere", P.hit_radius)

    -- KINEMATIC: it is part of the scenery and nothing should push it around,
    -- but it must still register contacts so the player's rounds can hit it.
    Physics.setBody(entity, "kinematic")
end

function onUpdate(entity, dt)
    local P = properties
    local t = entity.transform

    cooldown = cooldown - dt

    local player = Scene.findByTag("player")
    if player == nil then
        px = nil          -- forget the old position, or a respawn reads as a jump
        return
    end

    local pp = player.transform.position

    -- --- Measure how fast the player is going ------------------------------
    if px == nil or dt <= 0 then
        px, py, pz = pp.x, pp.y, pp.z
        return                       -- nothing to aim with on the first frame
    end
    local mx = (pp.x - px) / dt
    local my = (pp.y - py) / dt
    local mz = (pp.z - pz) / dt
    px, py, pz = pp.x, pp.y, pp.z
    -- Smoothed, because the raw figure is a difference of two large world
    -- coordinates and carries the floating-point noise of both. A gun that
    -- aimed at the noise would jitter and never settle inside its firing cone.
    local a = 1 - math.exp(-12 * dt)
    vx = vx + (mx - vx) * a
    vy = vy + (my - vy) * a
    vz = vz + (mz - vz) * a

    -- --- Is it worth engaging? ---------------------------------------------
    local gp = t.position
    local dx, dy, dz = pp.x - gp.x, pp.y - gp.y, pp.z - gp.z
    local dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    if dist > P.range then return end
    if dy < P.min_target_height then return end

    -- --- Where to aim -------------------------------------------------------
    -- Guess the flight time from the present distance, move the target along by
    -- that much, then re-measure against the moved point and do it again. Each
    -- pass gets closer because the distance to the predicted point is a better
    -- estimate of the real flight time than the distance to the current one.
    local aimx, aimy, aimz = pp.x, pp.y, pp.z
    for _ = 1, 2 do
        local ax, ay, az = aimx - gp.x, aimy - gp.y, aimz - gp.z
        local flight = math.sqrt(ax * ax + ay * ay + az * az) / P.shell_speed
        aimx = pp.x + vx * flight
        aimy = pp.y + vy * flight
        aimz = pp.z + vz * flight
    end

    -- Slew toward the aiming point, no faster than the mounting allows. This is
    -- what gives a fast mover a chance: the gun is always chasing.
    t:rotateToward(aimx, aimy, aimz, P.turn_rate * dt)

    -- --- Fire, if actually lined up -----------------------------------------
    local f = t:forward()
    local ex, ey, ez = aimx - gp.x, aimy - gp.y, aimz - gp.z
    local elen = math.sqrt(ex * ex + ey * ey + ez * ez)
    if elen < 0.001 then return end
    local dot = (f.x * ex + f.y * ey + f.z * ez) / elen
    if dot >  1 then dot =  1 end
    if dot < -1 then dot = -1 end
    if math.deg(math.acos(dot)) > P.fire_angle then return end

    if cooldown > 0 then return end
    cooldown = P.fire_rate

    -- The shell is the same round the helicopters fire: already a rigid body
    -- with continuous collision, already damages the player and ignores its own
    -- side. Reusing it means a change to how enemy fire behaves applies here too
    -- instead of the two drifting apart.
    --
    -- No velocity is passed because the gun is bolted to the ground: unlike an
    -- aircraft it has no motion of its own to add to the muzzle velocity.
    Scene.spawn("AAShell",
        gp.x + f.x * P.muzzle, gp.y + f.y * P.muzzle, gp.z + f.z * P.muzzle,
        f.x, f.y, f.z,
        "assets/scripts/enemy_bullet.lua")

    Fx.burst("muzzle", gp.x + f.x * P.muzzle, gp.y + f.y * P.muzzle,
             gp.z + f.z * P.muzzle, 1.5)
    Audio.playAt("shot", gp.x, gp.y, gp.z)
end

-- Blow up when destroyed, and pay out. Ground targets are worth more than a
-- helicopter because killing one means flying into its own engagement range.
function onDestroy(entity)
    local p = entity.transform.position
    Hud.add("score", 3)
    Fx.burst("explosion", p.x, p.y, p.z, 1.5)
    Audio.playAt("explosion", p.x, p.y, p.z)
end
