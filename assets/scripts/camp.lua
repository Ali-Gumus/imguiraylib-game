-- camp.lua
-- =============================================================================
-- The camp itself, as a single target.
--
-- This entity is invisible. It carries no shape - only a large collision box
-- covering the whole built-up area, and the health of the position as a whole.
-- The buildings inside it are scenery, and hitting ANY of them is hitting this.
--
-- WHY THE CLUSTER IS ONE HITBOX RATHER THAN A HEAP OF SEPARATE ONES.
-- A camp is a place, and destroying it should be one act. With a hitbox per hut
-- the player has to grind down eight or nine little objects one at a time,
-- checking whether each has gone, which turns an attack run into an inventory.
-- Sharing a hitbox means a pass over the camp damages THE CAMP, and when it has
-- taken enough the whole position goes up at once - which is both what an air
-- strike looks like and far clearer to read from a cockpit.
--
-- It also fixes something subtler: a building's own box would have to be hit
-- exactly, so bombing the gap between two huts would score nothing. One box over
-- the whole area means putting fire into the camp counts, which is the thing the
-- player is actually trying to do.
--
-- The anti-aircraft vehicles are NOT part of this box. They sit outside it, are
-- separately shootable, and can be picked off one by one from a distance - but
-- they die with the camp when it falls, because a battery whose position has
-- been levelled has nothing left to defend.
-- =============================================================================

properties = {
    -- Half the width and depth of the box covering the camp's buildings, in
    -- metres. It has to cover the outbuildings, which camps.lua rings around
    -- the HQ, without reaching so far that shots nowhere near anything count.
    half_extent = 110,

    -- Half its height. The box stands from the ground up: tall enough that a
    -- shallow attack run puts rounds into it rather than over it, low enough
    -- that it is not an invisible wall in the sky.
    half_height = 25,

    -- How far out its death reaches, in metres. Everything of the camp's within
    -- this goes up with it, including the guns ringed outside the box.
    blast_radius = 420,

    -- Awarded when the position falls. Worth far more than any single vehicle:
    -- it is the objective, and getting to it means going through the guns.
    points = 60,
}

function onStart(entity)
    local P = properties

    -- One box over the whole position. Collider sizes are HALF-extents and are
    -- in world units regardless of the entity's scale, which is why nothing here
    -- touches the transform - this entity has no size of its own to speak of.
    Scene.setCollider(entity, "box", P.half_extent, P.half_height, P.half_extent)

    -- KINEMATIC, so it registers the player's rounds without being shoved about
    -- by them. It is a place, not an object.
    Physics.setBody(entity, "kinematic")
end

function onDestroy(entity)
    local P = properties
    local p = entity.transform.position

    Hud.add("score", P.points)

    -- Take the whole position with it. Each of these is destroyed in the ordinary
    -- way, so every building and every vehicle runs its OWN onDestroy and blows
    -- up in its own place - the camp comes apart across its whole footprint
    -- rather than in one lump at the middle.
    -- Three tags, because the camp's parts are labelled by WHAT THEY ARE:
    -- "hq" the command building, "base" the outbuildings, "aa" the vehicles.
    -- structure.lua reads its own tag to know which size to build, which is why
    -- the two kinds of building are not simply lumped under one label.
    Scene.destroyNear("hq",   p.x, p.y, p.z, P.blast_radius)
    Scene.destroyNear("base", p.x, p.y, p.z, P.blast_radius)
    Scene.destroyNear("aa",   p.x, p.y, p.z, P.blast_radius)

    -- And a blast at the centre of it, over the top of all of those.
    Fx.burst("explosion", p.x, p.y, p.z, 30.0)
    Audio.playAt("explosion", p.x, p.y, p.z)
end
