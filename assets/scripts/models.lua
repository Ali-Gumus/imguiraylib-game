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

-- The enemy helicopter. Its pivot is a long way from its body and it was
-- authored facing backwards, hence the large offsets.
Model.define("heli", {
    file  = "assets/models/hind_attack_helicopter.glb",
    scale = 0.01,
    rot   = {0, 180, 0},
    pos   = {0, -240, 250},
})

-- The player's jet, for reference and for spawning allies later.
Model.define("jet", {
    file  = "assets/models/f-14_tomcat_top_gun_gear_up_downloadable.glb",
    scale = 8.0,
    rot   = {0, -90, 0},
    pos   = {0, 0, 0},
})
