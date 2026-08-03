-- models.lua
-- =============================================================================
-- Every named model set-up in the game, described as data.
--
-- Getting an imported model to sit right on an entity takes four things: which
-- file, how big, which way it faces, and where its pivot is. Naming that
-- combination once means a spawning script only has to say the name:
--
--     Scene.spawn("Enemy", x, y, z, dx, dy, dz, script, "enemy", 3, "heli")
--
-- The engine re-reads this file at startup and on every Play, so retuning a
-- model is a matter of changing a number here and pressing Play - no rebuild.
-- The node editor also lists these names in a dropdown, so a graph never has to
-- type a file path.
--
-- Fields (all optional except the file):
--   file    path to the model, relative to the project root (.obj/.glb/.gltf)
--   scale   how much to resize the MODEL. Model files vary enormously in what
--           one unit means; this brings them into the game's own scale, where
--           one unit is one metre. It resizes the drawing only - the entity
--           keeps its true size, so colliders, speeds and child objects are all
--           unaffected by how large the artist happened to export the mesh.
--   rot     euler degrees turning the MODEL to face this engine's forward,
--           which is -Z, with +Y up. Sideways: {0,90,0} or {0,-90,0}.
--           Backwards: {0,180,0}. Lying on its back: {-90,0,0}.
--   pos     shift bringing the model onto the entity's origin, in the MODEL's
--           own units (so it is measured before `scale` is applied, and the
--           numbers can be large). Use the Model component's "Centre On Origin"
--           button on an entity in the editor to find these, then copy them.
--
-- Why the pivot matters: everything rotates about the entity's origin, so a
-- model whose pivot sits outside its own body swings around a point in mid-air
-- instead of banking about itself.
-- =============================================================================

-- How the scales below were arrived at, since a number like 0.010318 plainly
-- was not typed from memory: load the file, read the size of its mesh in its
-- own units, and divide a known real-world dimension by it. A model whose
-- longest axis measures 1675 units and depicts an aircraft with a 17.3-metre
-- rotor is telling you that one of its units is 17.3/1675 of a metre.
--
-- The check that makes this trustworthy is doing it TWICE, with two different
-- real dimensions, and seeing whether the two scales agree. One measurement
-- can be read off the wrong axis and still look plausible; two that agree to a
-- fraction of a percent cannot both be wrong the same way.

-- The enemy helicopter, a Mi-24. Its pivot is a long way from its body and it
-- was authored facing backwards, hence the large offsets.
--
-- Measured 1675.4 x 563.9 x 1920.9 in its own units. The rotor disc is circular,
-- so the width is the rotor's 17.3-metre diameter: 17.3/1675.4 = 0.010318. The
-- length then works out at 19.82 metres against a real 19.79 with the rotors
-- turning, which is the second measurement agreeing with the first.
Model.define("heli", {
    file  = "assets/models/hind_attack_helicopter.glb",
    scale = 0.010318,
    rot   = {0, 180, 0},
    pos   = {-9.9, -276.8, 227.2},
})

-- The player's jet, for reference and for spawning allies later. Nothing
-- spawns this yet.
--
-- Measured 250.0 x 59.4 x 152.6 in its own units, and built along X rather
-- than Z, which is what the rot below turns round. An F-14 is 19.1 metres
-- long, so 19.1/250 = 0.0764. Its wings are swept in this model, and the
-- width that scale implies is 11.66 metres against a real swept span of
-- 11.58 -- again, a second dimension agreeing.
--
-- Its pivot sits 160 units out along X, which is over twelve metres once
-- scaled: far outside the aircraft, so without the shift below it would swing
-- around a point in mid-air rather than bank about itself.
Model.define("jet", {
    file  = "assets/models/f-14_tomcat_top_gun_gear_up_downloadable.glb",
    scale = 0.0764,
    rot   = {0, -90, 0},
    pos   = {-160.8, -94.1, -29.7},
})
