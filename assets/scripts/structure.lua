-- structure.lua
-- =============================================================================
-- A building in an enemy camp: the headquarters itself, or one of the smaller
-- huts and depots around it. Both are targets to be bombed flat.
--
-- It does nothing but stand there and be destructible. There is no behaviour to
-- write - the interest is in getting to it past the guns defending it.
--
-- ONE SCRIPT, TWO SIZES, CHOSEN BY TAG. An entity is spawned with a tag, and
-- this reads its own tag to decide what it is: "hq" makes the large command
-- building, anything else makes an outbuilding. That is a deliberate alternative
-- to writing two nearly identical scripts, or to passing a size through
-- Scene.spawn, which has no argument for one. The tag is already there, already
-- means "what this entity is", and is already what the player's bullets test.
-- =============================================================================

properties = {
    -- The command building: big enough to spot from the air, which is the whole
    -- point of it being the objective.
    hq_width  = 44,
    hq_height = 16,
    hq_depth  = 30,

    -- The lesser buildings around it.
    hut_width  = 14,
    hut_height = 7,
    hut_depth  = 14,

    -- What each is worth. An HQ ends a camp, so it pays accordingly.
    hq_points  = 25,
    hut_points = 4,
}

-- Worked out in onStart and kept, so onDestroy knows which of the two this was
-- without asking again.
local isHq = false

function onStart(entity)
    local P = properties
    isHq = (entity.tag == "hq")

    local w = isHq and P.hq_width  or P.hut_width
    local h = isHq and P.hq_height or P.hut_height
    local d = isHq and P.hq_depth  or P.hut_depth

    -- The entity is spawned as a unit cube, so its scale IS its size in metres.
    local t = entity.transform
    t.scale.x, t.scale.y, t.scale.z = w, h, d

    -- A matching collision box. Collider sizes are HALF-extents and are in world
    -- units regardless of the entity's scale, so these are the real dimensions
    -- halved rather than the scale reused.
    Scene.setCollider(entity, "box", w * 0.5, h * 0.5, d * 0.5)

    -- KINEMATIC: a building is not pushed about by what hits it, but it must
    -- still register contacts so the player's rounds can damage it.
    Physics.setBody(entity, "kinematic")
end

function onDestroy(entity)
    local p = entity.transform.position
    Hud.add("score", isHq and properties.hq_points or properties.hut_points)

    -- A command building goes up far harder than a hut, which is what makes
    -- finishing a camp feel like finishing something.
    Fx.burst("explosion", p.x, p.y, p.z, isHq and 4.0 or 1.5)
    Audio.playAt("explosion", p.x, p.y, p.z)
end
