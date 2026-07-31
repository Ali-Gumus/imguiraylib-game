-- GENERATED from a node graph. Edit the GRAPH, not this file --
-- your changes here are overwritten on the next generate.

function onUpdate(entity, dt)
    if Input.keyDown("W") then
    entity.transform.position.x = entity.transform.position.x + 0.000 * dt
    entity.transform.position.y = entity.transform.position.y + 0.000 * dt
    entity.transform.position.z = entity.transform.position.z + -5.000 * dt
    end
    if Input.keyDown("A") then
    entity.transform.position.x = entity.transform.position.x + -5.000 * dt
    entity.transform.position.y = entity.transform.position.y + 0.000 * dt
    entity.transform.position.z = entity.transform.position.z + 0.000 * dt
    end
    if Input.keyDown("S") then
    entity.transform.position.x = entity.transform.position.x + 0.000 * dt
    entity.transform.position.y = entity.transform.position.y + 0.000 * dt
    entity.transform.position.z = entity.transform.position.z + 5.000 * dt
    end
    if Input.keyDown("D") then
    entity.transform.position.x = entity.transform.position.x + 5.000 * dt
    entity.transform.position.y = entity.transform.position.y + 0.000 * dt
    entity.transform.position.z = entity.transform.position.z + 0.000 * dt
    end
    if Input.keyDown("SHIFT") then
    entity.transform.position.x = entity.transform.position.x + 0.000 * dt
    entity.transform.position.y = entity.transform.position.y + -5.000 * dt
    entity.transform.position.z = entity.transform.position.z + 0.000 * dt
    end
    if Input.keyDown("SPACE") then
    entity.transform.position.x = entity.transform.position.x + 0.000 * dt
    entity.transform.position.y = entity.transform.position.y + 5.000 * dt
    entity.transform.position.z = entity.transform.position.z + 0.000 * dt
    end
end

