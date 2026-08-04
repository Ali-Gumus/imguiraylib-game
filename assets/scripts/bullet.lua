-- bullet.lua
-- =============================================================================
-- A single player bullet, simulated as a real physical object.
--
-- The bullet does not steer itself and is not moved by this script at all. It
-- is launched once, at the moment it is created, and from then on the physics
-- engine carries it: it keeps its speed, falls under gravity, and stops when it
-- runs into something. That is what produces bullet drop - a shot at a distant
-- target has to be aimed a little high, because the round is falling the whole
-- way there - without any of it being written here.
--
-- Two settings deserve a word, because they are the difference between a bullet
-- that behaves and one that does not:
--
--   * CONTINUOUS collision. The simulation advances in steps of a sixtieth of a
--     second. At 300 units a second a bullet covers five units in one step, so
--     it can be in front of a wall on one step and past it on the next, never
--     overlapping it on either, and sail straight through. Continuous collision
--     sweeps the whole path instead of sampling its end. Without it, fast shots
--     silently miss thin targets.
--
--   * GRAVITY. Drop is independent of speed: a bullet falls the same distance
--     in three seconds whether it is travelling at 200 or 800. What changes is
--     how FAR it has gone by then, which is why a faster round has a flatter
--     trajectory. Raise `speed` for a flatter shot, or lower `gravity` below 1
--     if the drop feels heavier than the game wants.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
--
-- Modelled on the M61 Vulcan, the 20 mm rotary cannon an F-16 actually carries:
-- a muzzle velocity of 1036 metres per second and a round of about 0.1 kg.
--
-- `speed` IS THE SPEED RELATIVE TO WHATEVER FIRED THE ROUND, not through the
-- world, and that distinction is worth understanding because this script does
-- not appear to do anything about it.
--
-- A real gun's muzzle velocity is measured against the gun, so a round leaves a
-- moving aircraft at 1036 metres per second PLUS the aircraft's own velocity.
-- What is launched below is only the first half of that. The second half is
-- added by the engine: `Scene.spawn` takes the spawner's velocity as its last
-- three arguments, and the scene adds it to this bullet's rigid body AFTER this
-- script's onStart has run. gun.lua measures the jet's motion and passes it.
--
-- The ordering is what makes it work, and it is also why the addition cannot
-- live in this file: a bullet has no way to ask who fired it, and Scene.spawn
-- queues the new entity for the end of the frame and returns nothing, so the gun
-- cannot reach the bullet afterwards either. The engine is the only party that
-- holds both.
--
-- Fired from something standing still - a ground turret - nothing is added and
-- the round simply leaves at `speed`, which is the same answer.
properties = {
    speed   = 1036,   -- muzzle velocity in metres per second (M61 Vulcan)
    life    = 5.0,    -- seconds before it gives up and removes itself
    damage  = 1,      -- hit points removed from an enemy on impact
    -- A real 20 mm round is 20 mm ACROSS, and a projectile that small moving
    -- this fast is a poor thing to simulate: it slips between collision checks
    -- and misses. This is deliberately far bigger than scale so that hits
    -- register reliably, which matters more than a tracer's exact width.
    radius  = 0.25,   -- physical size of the round
    mass    = 0.1,    -- kilograms; affects how hard it shoves what it hits
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
    -- authored bullet prefab keeps whatever was set up in the editor.
    Scene.setCollider(entity, "sphere", P.radius)
    Physics.setBody(entity, "dynamic", P.mass, P.gravity, true)

    -- Launch it along the way it is facing. The gun aimed the bullet when it
    -- spawned it, so "forward" is already the direction of the shot.
    --
    -- The body does not exist yet - an entity spawned this frame joins the
    -- simulation at the end of it - so this is recorded as the velocity the
    -- bullet will be born with rather than applied to something. The script
    -- does not have to care which of the two happened.
    --
    -- This is the MUZZLE velocity only. The velocity of whatever fired the round
    -- is added to it once this function returns - see the note at the top - so
    -- setting it outright here does not throw that away.
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
    -- Never detonate on the aircraft that fired it. A bullet leaves the barrel
    -- close to its own jet, and clipping the nose on the way out should be
    -- harmless rather than fatal.
    if other.tag == "player" then
        return
    end

    -- WHAT COUNTS AS A TARGET IS "HAS HEALTH", not a list of tags.
    --
    -- The obvious way to write this is to name the tags a bullet may hurt, and
    -- it works right up until something new is added to the game - a gun
    -- emplacement, a hut, a headquarters - at which point rounds pass harmlessly
    -- through the new thing and the reason is a list in a file nobody thought to
    -- look in. Asking whether the target HAS health instead means anything built
    -- to be destroyed is destructible the moment it exists, without this script
    -- knowing it was invented.
    --
    -- Scene.health returns current and maximum; the guard is on the MAXIMUM,
    -- because a maximum of zero means "no Health component at all" whereas a
    -- current of zero is a real reading of something already dead.
    local _, maxHp = Scene.health(other)
    if maxHp > 0 then
        -- The target awards score itself when it dies (its own onDestroy), so
        -- the bullet stays a pure projectile.
        Scene.damage(other, properties.damage)
        Fx.burst("spark", x, y, z)
    else
        -- Anything else - the ground, scenery - throws up a puff instead.
        Fx.burst("explosion", x, y, z, 0.5)
    end

    -- Heard from where it landed, so a shot striking a distant hill is faint
    -- and off to one side rather than going off inside the player's head.
    Audio.playAt("impact", x, y, z)
    Scene.destroy(entity)
end
