-- bullet.lua
-- =============================================================================
-- A single bullet. It flies straight forward, and if it passes close enough to
-- an entity tagged "enemy" it damages that enemy and removes itself. If it hits
-- nothing it disappears after a short lifetime so bullets never pile up forever.
--
-- A bullet is created by the gun with scene.spawn(...), already positioned and
-- rotated to face the firing direction, so this script only has to move it
-- straight ahead and watch for a hit.
-- =============================================================================

local speed      = 70    -- travel speed in world units per second
local life       = 2.0   -- seconds the bullet exists before self-destructing
local damage     = 1     -- hit points removed from an enemy on impact
local hit_radius = 1.0   -- how near an enemy must be (world units) to count as a hit

-- Age of this bullet in seconds. Starts at 0 and grows each frame.
local age = 0

-- Called once when the bullet is created.
function on_start(entity)
    -- Spawned entities are a full 1x1x1 cube by default, which is too big for a
    -- bullet, so shrink the visual to a small 0.25 cube. scale is a multiplier
    -- on the base size, applied on each axis.
    entity.transform.scale.x = 0.25
    entity.transform.scale.y = 0.25
    entity.transform.scale.z = 0.25
end

-- Called every frame. `dt` is the seconds since the previous frame.
function on_update(entity, dt)
    -- Move forward along the bullet's own facing. translate_local(0,0,-d) moves
    -- `d` units toward the nose (the local -Z direction is "forward").
    entity.transform:translate_local(0, 0, -speed * dt)

    -- Look for a target: the nearest entity tagged "enemy" within hit_radius of
    -- the bullet's current position. Returns that entity, or nil if none.
    local p = entity.transform.position
    local target = scene.nearest("enemy", p.x, p.y, p.z, hit_radius)

    -- `~=` means "not equal"; so this is true when a target was found.
    if target ~= nil then
        scene.damage(target, damage)   -- subtract from the target's health
        scene.destroy(entity)          -- the bullet is used up, remove it
        return                         -- stop here; nothing else to do this frame
    end

    -- No hit this frame: age the bullet, and remove it once it outlives `life`.
    age = age + dt
    if age > life then
        scene.destroy(entity)
    end
end
