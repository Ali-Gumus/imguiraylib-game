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
--   texture an image painted over the model, relative to the project root.
--           LEAVE IT OUT for a model that carries its own textures inside it,
--           which a .glb normally does.
--
--           It is for the other kind: a mesh distributed alongside loose image
--           files. An .obj always is - its .mtl names images by a path that
--           stops being true the moment the files are moved - and a .gltf often
--           is too. When the material ends up with no image, the mesh draws
--           with a plain white one, so the model appears in the right place at
--           the right size in a flat untextured colour. That looks far more
--           like a lighting problem than a missing file, which is what makes it
--           worth knowing about.
--
--           One image covers every material on the model. A mesh split into
--           several parts each wanting its own image cannot be described here;
--           that needs the model file's own material data to be right.
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

-- An F-16, for spawning allies or a second player aircraft. Nothing spawns this
-- yet, so it costs nothing until something does.
--
-- These numbers are COPIED FROM THE PLAYER'S OWN JET rather than measured
-- again. That aircraft is authored in the scene file with its model already
-- sized and centred, so anything spawned from here comes out identical to the
-- one being flown - which is the point of an ally looking like a wingman rather
-- than like a different aeroplane.
--
-- If the player's jet is ever re-tuned in the Inspector, copy the three numbers
-- across again: the scene file and this define are two separate records of the
-- same set-up and nothing keeps them in step automatically.
--
-- It used to name an F-14 that is no longer in the project, which loaded
-- nothing and reported `mdl?` in the toolbar.
Model.define("jet", {
    file  = "assets/models/f16-c_falcon.glb",
    scale = 1.71,
    rot   = {0, 0, 0},
    pos   = {0, -10, 0.8},
})

-- =============================================================================
-- GROUND MODELS FOR THE ENEMY CAMPS
--
-- These two are placeholders POINTING AT FILES THAT ARE NOT IN THE REPOSITORY.
-- That is deliberate and it is safe: a model whose file is missing falls back to
-- the entity's primitive shape, and the toolbar shows an `mdl?` badge naming
-- what it could not find. So the camps work as boxes today and become vehicles
-- and hangars the moment the files appear, with no script or C++ change.
--
-- To use your own, drop the file in assets/models/ and either name it as below
-- or edit the `file` line here. Then measure it the way the two aircraft above
-- were measured - pick a real dimension you know, divide it by the model's own
-- span on that axis, and CHECK THE ANSWER against a second dimension. The scale
-- and offsets below are guesses standing in until that is done, because there is
-- no file to measure yet, and a guessed scale is exactly what §5.28 warns about.
--
-- The engine convention is nose along -Z and +Y up; `rot` turns a model that was
-- authored some other way, and `pos` shifts a mesh whose pivot is not at its
-- middle.
-- =============================================================================

-- The anti-aircraft gun emplacement.
--
-- Measured with raylib: 389.2 x 305.3 x 364.8 in its own units, so at the 0.1
-- below it is drawn about 38.9 x 30.5 x 36.5 METRES. That is roughly ten times
-- a real towed anti-aircraft gun, and it is deliberate rather than an error:
-- these stand three to nine kilometres away, and something the true size is
-- invisible at that range while still shooting at you, which reads as being
-- shot at by nothing. `hit_radius` in aa_gun.lua is 20 - a 40 m ball - which
-- was tuned to this drawn size, so the collider and the model do agree.
--
-- Its pivot sits at the BOTTOM of the mesh, which is what you want for
-- something standing on the ground: the entity's position is where its base is.
-- Left uncentred for that reason. Note it is 31.7 units - about 3.2 m - off
-- centre SIDEWAYS, which is why aa_gun.lua's muzzle offset has a sideways axis.
--
-- aa_gun.lua turns the whole entity to aim, so it will tip about that base
-- rather than traversing a turret. Set `rot` if it ends up facing the wrong way.
--
-- (The scale is NOT derived from a known real dimension the way the other
-- models here are. The mesh is nearly cubic, so there is no obvious length to
-- divide by, and 0.1 was chosen for visibility. The muzzle and explosion
-- offsets in aa_gun.lua are fitted to it, so changing it means retuning those.)
Model.define("aa_vehicle", {
    file    = "assets/models/mp_drum_artillery_dam.obj",
    texture = "assets/models/mp_drum_artillery_boforsbody_c.jpeg",
    scale   = 0.1,
    rot   = {0, 90, 0},
    pos   = {0, 0, 0},
})

-- The camp's main building.
--
-- Measured 15.43 x 5.62 x 18.90, and those are already METRES: 19 m deep and
-- 15 m wide is a real small-aircraft hangar, so this one was authored at scale
-- and wants none applied. Not every model is so obliging - check yours before
-- assuming 1.0.
Model.define("hangar", {
    file  = "assets/models/aircraft_hangar.glb",
    scale = 6.0,
    rot   = {0, 0, 0},
    pos   = {0, 0, 0},
})

-- The outbuildings, which are the same hangar at half size. A separate DEFINE
-- rather than a separate file: a camp of four identical full-size hangars reads
-- as a copy-paste, while a big one among smaller ones reads as a base. Give it
-- its own file whenever you have one.
Model.define("hut", {
    file  = "assets/models/aircraft_hangar.glb",
    scale = 4,
    rot   = {0, 0, 0},
    pos   = {0, 0, 0},
})
