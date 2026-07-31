#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/Model.h"

#include <string>

namespace eng {

void RegisterModelBindings(sol::state& lua) {
    lua.new_usertype<ModelComponent>("Model",
        // The path goes through a PROPERTY rather than the raw field, because
        // changing it has to reload the mesh. Writing the member directly would
        // leave the component naming one file and drawing another.
        "path", sol::property(
            [](const ModelComponent& m) { return m.path; },
            [](ModelComponent& m, const std::string& p) { m.SetPath(p); }),
        "scale",          &ModelComponent::scale,
        "positionOffset", &ModelComponent::positionOffset,
        "rotationOffset", &ModelComponent::rotationOffset,
        "tint",           &ModelComponent::tint,
        "triangleCount",  sol::property(&ModelComponent::TriangleCount)
    );
    RegisterComponentAccess<ModelComponent>(lua, "Model");
}

void DescribeModelBindings(LuaApiRegistry& api) {
    api.Usertype("Entity", "entity")
        .Method("addComponent_Model() -> Model", "Add a Model and return it")
        .Method("getComponent_Model() -> Model", "Its Model, or nil");

    auto m = api.Usertype("Model", "model");
    m.Prop("path",           "The model file. Assigning reloads it");
    m.Prop("scale",          "Resizes the MESH, leaving the entity its true size");
    m.Prop("positionOffset", "Shifts the mesh inside the entity, for a model whose pivot is off centre");
    m.Prop("rotationOffset", "Euler degrees, for a model authored facing the wrong way");
    m.Prop("tint",           "Colour multiplied over the model");
    m.Prop("triangleCount",  "How many triangles it draws. Read-only");
}

} // namespace eng
