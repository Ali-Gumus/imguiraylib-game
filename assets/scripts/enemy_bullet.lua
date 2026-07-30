-- enemy_bullet.lua
-- =============================================================================
-- A bullet fired by an enemy. Same as the player's bullet, except it looks for
-- the "player" tag instead of "enemy", so enemy fire hurts the player and not
-- other enemies.
-- =============================================================================

-- Tunable values, shown as editable fields in the Inspector.
--
-- 860 metres per second is the muzzle velocity of the 12.7 mm gun a Mi-24
-- carries. It has to be in that region for the enemy to be a threat at all: the
-- player's jet tops out at 685, so anything slower than that can simply be
-- outrun and enemy fire would never land.
--
-- WHY hit_radius IS SO LARGE. This bullet is moved by the script rather than by
-- the simulation, and it checks for a hit only once per frame, at wherever it
-- has arrived. At 860 metres per second it covers about 14 metres between one
-- check and the next, so it JUMPS over that distance without testing any of it.
-- A radius of a metre or two would therefore fly straight through the player
-- almost every time - the faster the round, the more reliably it misses. The
-- radius is sized to cover most of one frame's jump instead.
--
-- The real fix is the one the player's own bullet already uses: be a rigid body
-- with continuous collision and let the simulation sweep the whole path. Until
-- then this number has to stay tied to speed - raise one and the other must
-- follow.
properties = {
    speed      = 860,   -- muzzle velocity in metres per second
    life       = 3.0,   -- seconds before it self-destructs
    damage     = 1,     -- hit points removed from the player on impact
    hit_radius = 12,    -- covers one frame's travel; see the note above
}

local age = 0           -- seconds this bullet has existed (runtime state)

function on_start(entity)
    entity.transform.scale.x = 0.25
    entity.transform.scale.y = 0.25
    entity.transform.scale.z = 0.25
end

function on_update(entity, dt)
    local P = properties

    entity.transform:translate_local(0, 0, -P.speed * dt)

    local p = entity.transform.position
    -- scene.hit adds the player's hitRadius to our reach (sphere-vs-sphere).
    local target = scene.hit("player", p.x, p.y, p.z, P.hit_radius)
    if target ~= nil then
        scene.damage(target, P.damage)
        -- Sparks on the player, so being hit is visible and not just a number
        -- dropping on the health bar.
        fx.burst("spark", p.x, p.y, p.z)
        -- A distinct sound from the player's own hits: being shot has to be
        -- unmistakable, not something to work out from the health bar.
        -- Left unpositioned on purpose. This one is not a sound happening
        -- somewhere in the world so much as a message to the player that they
        -- are being hit, and it must never be faint or off to one side - it
        -- always fires right where the player is anyway.
        audio.play("hit_taken")
        scene.destroy(entity)
        return
    end

    age = age + dt
    if age > P.life then
        scene.destroy(entity)
    end
end
