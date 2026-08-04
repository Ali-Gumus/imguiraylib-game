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

    -- The entity is spawned as a unit cube, so its scale IS its size in metres -
    -- but ONLY while it is a cube. The entity's scale multiplies whatever it
    -- draws, a model included, and a model already carries its own size from
    -- models.lua. Applying these to one would stretch it 44 by 16 by 30, which
    -- reads as a broken import rather than a wrong number.
    if not entity:hasComponent_Model() then
        local t = entity.transform
        t.scale.x, t.scale.y, t.scale.z = w, h, d

        -- AND STAND IT UP. camps.lua places every building with its origin ON
        -- the ground, because that is where a model's pivot almost always is.
        -- A cube's origin is its MIDDLE, so left there it would be buried to
        -- the waist - it has to rise by half its own height.
        --
        -- Doing it here rather than in camps.lua is deliberate: this is the
        -- script that knows how big the cube is, and the correction has to
        -- follow that size. A lift written into the placement code would be a
        -- second copy of these numbers, silently wrong the moment either
        -- changed.
        t.position.y = t.position.y + h * 0.5
    end

    -- NO COLLIDER AND NO HEALTH, ON PURPOSE.
    --
    -- The camp shares one hitbox, carried by the invisible camp entity that
    -- covers the whole position (camp.lua). If a building had a box of its own
    -- it would be the thing rounds struck, and they would damage a building
    -- rather than the camp - so the shared hitbox would never be hit and the
    -- position could never be destroyed.
    --
    -- These are scenery: what the camp LOOKS like. They are removed by the camp
    -- when it falls, and their onDestroy below is what makes that read as the
    -- whole place coming apart rather than one explosion at the middle.
end

-- Runs when the camp that owns this building is destroyed and takes it with it.
-- No score is awarded here - the camp pays for the whole position at once, and
-- paying again per building would make the total depend on how many huts it
-- happened to have.
function onDestroy(entity)
    local p = entity.transform.position

    -- A command building goes up far harder than a hut, which is what gives the
    -- camp's collapse a shape instead of being one even wall of fire.
    Fx.burst("explosion", p.x, p.y, p.z, isHq and 4.0 or 1.5)
    Audio.playAt("explosion", p.x, p.y, p.z)
end
