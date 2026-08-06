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
    fire_rate = 0.5,

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

    -- WHERE THE END OF THE BARREL IS, as an offset from the entity's own
    -- position, in metres. Three numbers rather than one, because a muzzle is a
    -- POINT ON THE MODEL and a single distance along the nose can only ever
    -- describe a gun whose barrel comes out of its own origin.
    --
    -- The three axes are the GUN'S OWN, not the world's:
    --   muzzle_forward - down the barrel, the way it is aiming
    --   muzzle_up      - how high the barrel sits above the entity's position,
    --                    which is at ground level (a vehicle's wheels)
    --   muzzle_right   - sideways, for a barrel that is not on the centreline
    --
    -- Using the gun's own axes rather than the world's matters because
    -- aa_gun.lua turns the WHOLE ENTITY to aim, so the body tips back as it
    -- elevates. A point fixed in the model's axes tips with it and stays on the
    -- barrel; a fixed height in WORLD axes stays level while the model rises
    -- out from under it, and the flash drifts off the barrel exactly when the
    -- gun is doing the most interesting thing.
    --
    -- These depend entirely on HOW BIG THE MODEL IS DRAWN, which is `scale` in
    -- models.lua. The mesh was measured with raylib: 389.2 x 305.3 x 364.8 in
    -- its own units, so at the scale of 0.1 the game uses it is drawn about
    --     38.9 wide x 30.5 tall x 36.5 deep METRES,
    -- with its pivot at the BOTTOM (which is right for something standing on
    -- the ground) but sitting 3.2 m off centre sideways - which is one reason
    -- `muzzle_right` is worth having.
    --
    -- The values below follow from that: roughly the front face (half of 36.5)
    -- and high up the body. TUNE THEM BY EYE - they are a starting point from
    -- the geometry, not a claim about where the barrel art actually is.
    --
    -- The muzzle MUST also clear the gun's own collider, or the shell is born
    -- inside a solid object and is shoved aside instead of flying, which reads
    -- as the gun simply not shooting. The collider is a ball of `hit_radius`
    -- centred `hit_offset_y` above the entity, so what has to exceed
    -- `hit_radius` is the distance from THAT centre, not from the entity.
    -- Checked for the values below: the offset (0, 20, 18) is
    -- sqrt((20-5)^2 + 18^2) = 23.4 m from the ball's centre, clearing its 20 m
    -- radius. Re-check this whenever hit_radius or these numbers change.
    muzzle_forward = 18,
    muzzle_up      = 35,
    muzzle_right   = 0,

    -- The size of its hittable volume, so the player can destroy it.
    --
    -- A COLLIDER IS IN WORLD METRES AND IGNORES THE ENTITY'S SCALE, so this
    -- does not follow the model when its scale is changed in models.lua - it has
    -- to be set to match by hand. That is deliberate: collision sizes are
    -- gameplay, and having them silently track whatever size an artist happened
    -- to export a mesh at would make hitboxes an accident.
    hit_radius = 20,

    -- How far up the hittable ball sits inside the entity, in metres.
    --
    -- The entity's origin is on the ground - that is where a vehicle's wheels
    -- are - so a ball centred there is half buried, and rounds passing over the
    -- vehicle at body height miss it. Lifting it puts the volume where the
    -- vehicle actually is.
    hit_offset_y = 5,

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

    -- Where the death explosion goes off, and how big it is. Up the gun's own
    -- axis from its position, for the same reason the muzzle is: the entity's
    -- position is on the ground, so a blast centred there goes off under the
    -- vehicle instead of on it. Half the 30.5 m drawn height, measured above.
    explode_up   = 15,
    explode_size = 10,
}

-- Runtime state.
local cooldown = 0            -- seconds until it can fire again

-- The player's velocity, smoothed. Read from the engine rather than worked out
-- here: `entity.velocity` is sampled at one fixed point in every frame, so it
-- does not depend on where in the component list this script happens to sit and
-- does not wobble with the frame rate. Working it out locally as a change in
-- position over dt divides one frame's movement by another frame's duration,
-- and at 300 m/s a frame 15% out of step misreads the speed by 15% - which for
-- a gun that aims where the target WILL BE is a lead error of tens of metres.
local vx, vy, vz = 0, 0, 0
local has_vel = false

-- Where the end of the barrel is in the world, from the three offsets above.
--
-- The transform hands back its own three axes as world-space unit vectors, so
-- the offset is applied by walking that many metres along each in turn. This is
-- the standard way to turn a point ON an object into a point IN the world, and
-- it is why the offsets are written in the gun's axes rather than the world's:
-- the axes themselves carry whichever way the gun happens to be pointing.
--
-- Returns three numbers rather than a table, matching how the rest of this API
-- talks about positions (Fx.burst and Scene.spawn both take loose x, y, z).
local function muzzlePoint(t)
    local P = properties
    local p = t.position
    local f = t:forward()
    local u = t:up()
    local r = t:right()
    return p.x + f.x * P.muzzle_forward + u.x * P.muzzle_up + r.x * P.muzzle_right,
           p.y + f.y * P.muzzle_forward + u.y * P.muzzle_up + r.y * P.muzzle_right,
           p.z + f.z * P.muzzle_forward + u.z * P.muzzle_up + r.z * P.muzzle_right
end

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
    -- "Has a model component" is NOT the same as "has a model". A component
    -- whose file is missing exists perfectly happily and draws nothing, so
    -- trusting the component alone left these as invisible one-metre boxes that
    -- still shot at the player. Ask whether a mesh actually loaded.
    local mdl = entity:getComponent_Model()
    if mdl == nil or not mdl.loaded then
        local t = entity.transform
        t.scale.x, t.scale.y, t.scale.z = P.body_width, P.body_height, P.body_depth
        -- And lift it out of the ground. It is placed with its origin on the
        -- surface, which is where a model's pivot sits; a cube's origin is its
        -- middle, so it needs half its own height. See structure.lua for why
        -- this belongs here and not in the placement code.
        t.position.y = t.position.y + P.body_height * 0.5
    end

    -- Something for bullets to hit. Scene.setCollider only ADDS when there is
    -- none, so an emplacement given a collider in the editor keeps the authored
    -- one - which is why whether one existed already is checked FIRST.
    --
    -- Every field of a collider can be written straight from Lua through the
    -- Collider usertype; setCollider is only a shorthand for the common case,
    -- and it has no argument for an offset. Anything it cannot express is set
    -- here instead.
    local hadCollider = entity:hasComponent_Collider()
    Scene.setCollider(entity, "sphere", P.hit_radius)

    if not hadCollider then
        -- Only shape what this script just created. Writing these
        -- unconditionally would overwrite a hitbox someone had sized by hand in
        -- the editor, which is the one thing a script default must never do.
        local col = entity:getComponent_Collider()
        if col ~= nil then
            col.radius   = P.hit_radius
            -- Vector3 has no constructor bound in Lua, so its components are
            -- written one at a time. `offset` hands back the collider's own
            -- vector rather than a copy, so this really does move the shape.
            col.offset.x = 0
            col.offset.y = P.hit_offset_y
            col.offset.z = 0
        end
    end

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
        has_vel = false   -- forget the old reading, or a respawn reads as a jump
        return
    end

    local pp = player.transform.position

    -- --- How fast the player is going ---------------------------------------
    local pv = player.velocity
    local mx, my, mz = pv.x, pv.y, pv.z

    -- On the first frame with a target there is nothing smoothed yet, so the
    -- reading is taken whole rather than eased up from a standing start - which
    -- would have the gun aiming at where a stationary aircraft would be.
    if not has_vel then
        vx, vy, vz = mx, my, mz
        has_vel = true
        return                       -- nothing to aim with until the next frame
    end
    if dt <= 0 then return end

    -- Still smoothed. The engine's figure is measured over one frame and a
    -- target that is manoeuvring changes it every frame; a gun that chased each
    -- reading would jitter and never settle inside its own firing cone. This
    -- also means the lead is based on what the aircraft has been doing rather
    -- than on one instant of it, which is the right thing to extrapolate from.
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
    local mx, my, mz = muzzlePoint(t)
    Scene.spawn("AAShell", mx, my, mz, f.x, f.y, f.z,
                "assets/scripts/enemy_bullet.lua")

    -- The flash belongs at the end of the barrel, not at the vehicle, which is
    -- the whole reason the muzzle is a point rather than a distance.
    Fx.burst("muzzle", mx, my, mz, 1.5)
    Audio.playAt("shot", mx, my, mz)
end

-- Blow up when destroyed, and pay out. Ground targets are worth more than a
-- helicopter because killing one means flying into its own engagement range.
function onDestroy(entity)
    local P = properties
    local t = entity.transform
    local p = t.position
    Hud.add("score", 3)

    -- Centred on the BODY, not on the entity's position. The model's pivot is at
    -- the bottom of the mesh, because that is what a thing standing on the
    -- ground wants - the entity's position is where its wheels are. An explosion
    -- there goes off under the vehicle and reads as a blast in the dirt beside
    -- it rather than as the vehicle itself coming apart.
    --
    -- `explode_up` rather than the collider's own `hit_offset_y`, because the two
    -- answer different questions: one is where the thing LOOKS like it is, and
    -- the other is what a shell has to touch to count as a hit.
    local u = t:up()
    Fx.burst("explosion", p.x + u.x * P.explode_up,
                          p.y + u.y * P.explode_up,
                          p.z + u.z * P.explode_up, P.explode_size)
    Audio.playAt("explosion", p.x, p.y, p.z)
end
