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
        // Same reasoning as `path`: changing the texture has to reload, because
        // the shared model cache is keyed by the mesh AND the image.
        "texture", sol::property(
            [](const ModelComponent& m) { return m.texture; },
            [](ModelComponent& m, const std::string& t) { m.SetTexture(t); }),
        "triangleCount",  sol::property(&ModelComponent::TriangleCount),
        // Whether there is actually a model to draw. A script that sizes a
        // fallback primitive needs this rather than merely asking whether the
        // component exists: a component whose FILE is missing exists, and
        // trusting it leaves the entity as a one-metre box.
        "loaded",     sol::property(&ModelComponent::IsLoaded),
        "loadFailed", sol::property(&ModelComponent::LoadFailed)
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
    m.Prop("texture",
           "An image painted over every material. Empty means the model's own. "
           "Needed for a mesh shipped beside loose texture files");
    m.Prop("loaded",
           "Whether there is actually a mesh to draw. Read-only - a component "
           "whose file is missing still exists, so test this, not the component");
    m.Prop("loadFailed", "Whether the file was tried and could not be read. Read-only");
}

} // namespace eng
