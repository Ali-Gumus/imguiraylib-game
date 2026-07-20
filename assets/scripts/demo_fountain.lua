-- GENERATED from a node graph. Edit the GRAPH, not this file --
-- your changes here are overwritten on the next generate.

local acc_100 = 0

function on_update(entity, dt)
    acc_100 = acc_100 + dt
    if acc_100 >= 0.500 then
    acc_100 = acc_100 - 0.500
    scene.spawn_cube("drop",
        entity.transform.position.x + 0.000,
        entity.transform.position.y + 3.000,
        entity.transform.position.z + 0.000)
    end
    if input.key_down("SPACE") then
    print("goodbye")
    scene.destroy(entity)
    end
end
