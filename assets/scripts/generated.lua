-- GENERATED from a node graph. Edit the GRAPH, not this file --
-- your changes here are overwritten on the next generate.

function on_update(entity, dt)
    if input.key_down("W") then
    entity.transform.position.x = entity.transform.position.x + 0.000 * dt
    entity.transform.position.y = entity.transform.position.y + 0.000 * dt
    entity.transform.position.z = entity.transform.position.z + -5.000 * dt
    end
    if input.key_down("A") then
    entity.transform.position.x = entity.transform.position.x + -5.000 * dt
    entity.transform.position.y = entity.transform.position.y + 0.000 * dt
    entity.transform.position.z = entity.transform.position.z + 0.000 * dt
    end
    if input.key_down("S") then
    entity.transform.position.x = entity.transform.position.x + 0.000 * dt
    entity.transform.position.y = entity.transform.position.y + 0.000 * dt
    entity.transform.position.z = entity.transform.position.z + 5.000 * dt
    end
    if input.key_down("D") then
    entity.transform.position.x = entity.transform.position.x + 5.000 * dt
    entity.transform.position.y = entity.transform.position.y + 0.000 * dt
    entity.transform.position.z = entity.transform.position.z + 0.000 * dt
    end
end

