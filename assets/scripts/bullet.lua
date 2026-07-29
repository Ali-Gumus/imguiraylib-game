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
properties = {
    speed   = 1000,    -- launch speed in world units per second
    life    = 5.0,    -- seconds before it gives up and removes itself
    damage  = 1,      -- hit points removed from an enemy on impact
    radius  = 0.25,   -- physical size of the round
    mass    = 0.05,   -- kilograms; affects how hard it shoves what it hits
    gravity = 1.0,    -- 1 = full drop, 0 = flies perfectly straight
}

local age = 0           -- seconds this bullet has existed (runtime state)

function on_start(entity)
    local P = properties

    -- Spawned entities start as a full unit cube; shrink to a small tracer.
    entity.transform.scale.x = P.radius
    entity.transform.scale.y = P.radius
    entity.transform.scale.z = P.radius

    -- Give it a physical presence. Both calls only add what is missing, so an
    -- authored bullet prefab keeps whatever was set up in the editor.
    scene.set_collider(entity, "sphere", P.radius)
    physics.set_body(entity, "dynamic", P.mass, P.gravity, true)

    -- Launch it along the way it is facing. The gun aimed the bullet when it
    -- spawned it, so "forward" is already the direction of the shot.
    --
    -- The body does not exist yet - an entity spawned this frame joins the
    -- simulation at the end of it - so this is recorded as the velocity the
    -- bullet will be born with rather than applied to something. The script
    -- does not have to care which of the two happened.
    local f = entity.transform:forward()
    physics.set_velocity(entity, f.x * P.speed, f.y * P.speed, f.z * P.speed)
end

function on_update(entity, dt)
    -- The only thing left to do each frame: give up eventually. A round that
    -- hits nothing would otherwise fall for ever, and every one still in the
    -- air costs simulation time.
    age = age + dt
    if age > properties.life then
        scene.destroy(entity)
    end
end

-- Called by the engine when the physics simulation reports that this bullet has
-- struck something. `speed` is how fast the two were closing, and x, y, z is
-- where on the surfaces they met - which is where the effect belongs, rather
-- than at the bullet's centre.
function on_collision(entity, other, speed, x, y, z)
    -- Never detonate on the aircraft that fired it. A bullet leaves the barrel
    -- close to its own jet, and clipping the nose on the way out should be
    -- harmless rather than fatal.
    if other.tag == "player" then
        return
    end

    if other.tag == "enemy" then
        -- The enemy awards score itself when it dies (enemy.lua's on_destroy),
        -- so the bullet stays a pure projectile.
        scene.damage(other, properties.damage)
        fx.burst("spark", x, y, z)
    else
        -- Anything else - the ground, scenery - throws up a puff instead.
        fx.burst("explosion", x, y, z, 0.5)
    end

    -- Heard from where it landed, so a shot striking a distant hill is faint
    -- and off to one side rather than going off inside the player's head.
    audio.play_at("impact", x, y, z)
    scene.destroy(entity)
end
