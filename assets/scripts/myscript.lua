-- GENERATED from a node graph. Edit the GRAPH, not this file --

properties = {
    distance = 9,
    height = 3,
    stiffness = 4,
}

function on_update(entity, dt)
    local jet100 = scene.find("Jet")
    if jet100 ~= nil then
        local cd100 = properties.distance
        local ch100 = properties.height
        local cs100 = properties.stiffness
        local jf100 = jet100.transform:forward()
        local dx100 = jet100.transform.position.x - jf100.x * cd100
        local dy100 = jet100.transform.position.y - jf100.y * cd100 + ch100
        local dz100 = jet100.transform.position.z - jf100.z * cd100
        local ca100 = 1 - math.exp(-cs100 * dt)
        entity.transform.position.x = entity.transform.position.x + (dx100 - entity.transform.position.x) * ca100
        entity.transform.position.y = entity.transform.position.y + (dy100 - entity.transform.position.y) * ca100
        entity.transform.position.z = entity.transform.position.z + (dz100 - entity.transform.position.z) * ca100
        entity.transform:look_at(jet100.transform.position.x, jet100.transform.position.y, jet100.transform.position.z)
    end
end

