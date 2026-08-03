-- enemy_bullet.lua
-- =============================================================================
-- A bullet fired by an enemy, simulated as a real physical object.
--
-- Same as the player's bullet (bullet.lua), except for which side it hurts: it
-- damages the "player" tag and deliberately ignores the "enemy" tag, so enemy
-- fire cannot shoot down the aircraft that fired it or its wingmen.
--
-- IT IS NOT MOVED BY THIS SCRIPT. It is launched once, at the moment it is
-- created, and from then on the physics engine carries it. That is a change from
-- how this round used to work, and the reason is worth understanding because it
-- is the same trap that catches every fast projectile:
--
--   A script-moved bullet advances to a new position each frame and then asks
--   "is the player within so many metres of me?". It never tests any of the
--   ground it just covered. At 860 metres per second a round jumps about 14
--   metres between one test and the next, so a player 5 metres wide sitting
--   anywhere in that gap is simply not noticed - and the FASTER the round, the
--   more reliably it misses. It used to be papered over with a 12-metre hit
--   radius sized to cover one frame's jump, which meant near misses counted as
--   hits and the two numbers had to be kept in step by hand for ever.
--
--   A simulated bullet with CONTINUOUS collision sweeps the whole path between
--   one step and the next instead of sampling its end, so it hits what it
--   actually passes through, at any speed, with a hitbox its own real size.
--
-- Being simulated also brings bullet drop, ground and scenery impacts, and the
-- shooter's own velocity, all for free rather than as code in this file.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
--
-- Modelled on the 12.7 mm gun a Mi-24 carries: a muzzle velocity of 860 metres
-- per second and a round of about 50 grams. It has to be in that region for the
-- enemy to be a threat at all - the player's jet tops out at 685, so a round
-- much slower than that could simply be outrun and enemy fire would never land.
--
-- As with the player's gun, `speed` is the speed RELATIVE TO THE SHOOTER. The
-- firing helicopter's own velocity is added by the engine after this script's
-- onStart has run, because enemy.lua passes it to Scene.spawn - so a round from
-- an enemy flying towards the player arrives slightly faster than one from an
-- enemy flying away, exactly as it should.
properties = {
    speed   = 860,    -- muzzle velocity in metres per second, relative to the gun
    life    = 3.0,    -- seconds before it gives up and removes itself
    damage  = 1,      -- hit points removed from the player on impact
    -- A real 12.7 mm round is 12.7 mm ACROSS, and a projectile that small moving
    -- this fast is a poor thing to simulate even with a swept path. This is
    -- deliberately far bigger than scale so that hits register reliably, which
    -- matters more than a tracer's exact width. It is still nearly fifty times
    -- smaller than the 12-metre radius this script used to need.
    radius  = 0.25,   -- physical size of the round
    mass    = 0.05,   -- kilograms; affects how hard it shoves what it hits
    gravity = 1.0,    -- 1 = full drop, 0 = flies perfectly straight
}

local age = 0           -- seconds this bullet has existed (runtime state)

function onStart(entity)
    local P = properties

    -- Spawned entities start as a full unit cube; shrink to a small tracer.
    entity.transform.scale.x = P.radius
    entity.transform.scale.y = P.radius
    entity.transform.scale.z = P.radius

    -- Give it a physical presence. Both calls only add what is missing, so an
    -- authored bullet prefab would keep whatever was set up in the editor.
    --
    -- CONTINUOUS collision is the third argument to setBody's tail and is not
    -- optional in spirit: without it this round is back to sampling its position
    -- once per simulation step and tunnelling through the player, which is the
    -- whole problem being solved here.
    Scene.setCollider(entity, "sphere", P.radius)
    Physics.setBody(entity, "dynamic", P.mass, P.gravity, true)

    -- Launch it along the way it is facing. The enemy aimed the bullet when it
    -- spawned it, so "forward" is already the direction of the shot.
    --
    -- The body does not exist yet - an entity spawned this frame joins the
    -- simulation at the end of it - so this is recorded as the velocity the
    -- bullet will be born with rather than applied to something.
    --
    -- This is the MUZZLE velocity only; the firing helicopter's own velocity is
    -- added to it once this function returns, so setting it outright here does
    -- not throw that away.
    local f = entity.transform:forward()
    Physics.setVelocity(entity, f.x * P.speed, f.y * P.speed, f.z * P.speed)
end

function onUpdate(entity, dt)
    -- The only thing left to do each frame: give up eventually. A round that
    -- hits nothing would otherwise fall for ever, and every one still in the
    -- air costs simulation time.
    age = age + dt
    if age > properties.life then
        Scene.destroy(entity)
    end
end

-- Called by the engine when the physics simulation reports that this bullet has
-- struck something. `speed` is how fast the two were closing, and x, y, z is
-- where on the surfaces they met - which is where the effect belongs, rather
-- than at the bullet's centre.
function onCollision(entity, other, speed, x, y, z)
    -- Never detonate on its own side. A round leaves the barrel close to the
    -- helicopter that fired it and passes near its wingmen, and neither should
    -- be shot down by their own squadron - so this ignores the whole tag rather
    -- than trying to work out which enemy pulled the trigger. It is the mirror
    -- of bullet.lua, which ignores "player" for exactly the same reason.
    if other.tag == "enemy" then
        return
    end

    if other.tag == "player" then
        Scene.damage(other, properties.damage)
        -- Sparks on the player, so being hit is visible and not just a number
        -- dropping on the health bar.
        Fx.burst("spark", x, y, z)
        -- A distinct sound from the player's own hits: being shot has to be
        -- unmistakable, not something to work out from the health bar.
        -- Left UNPOSITIONED on purpose. This one is not a sound happening
        -- somewhere in the world so much as a message to the player that they
        -- are being hit, and it must never be faint or off to one side - and it
        -- always fires right where the player is anyway.
        Audio.play("hit_taken")
    else
        -- Anything else - the ground, scenery - throws up a puff instead. Enemy
        -- fire that misses now visibly strikes the landscape rather than
        -- vanishing into it, which is what makes near misses readable.
        Fx.burst("explosion", x, y, z, 0.5)
        Audio.playAt("impact", x, y, z)
    end

    Scene.destroy(entity)
end
