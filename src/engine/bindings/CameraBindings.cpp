#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/components/Camera.h"
#include "engine/Scene.h"

#include <string>

namespace eng {

void RegisterCameraBindings(sol::state& lua) {
    // The projection choice as a NAMED enum rather than a bare boolean or a
    // magic string. `Projection.Orthographic` says what it is at the call site;
    // `true` says nothing, and a typo in "orthographic" fails silently.
    lua.new_enum<bool>("Projection", {
        {"Perspective",  false},
        {"Orthographic", true},
    });

    // The component itself. Fields are exposed as PROPERTIES - sol::property
    // pairs a getter with a setter - so a script writes `camera.fovy = 70`
    // rather than `camera:setFovy(70)`. That reads like the data it is, and it
    // is the convention the rest of this API follows for plain values.
    lua.new_usertype<CameraComponent>("Camera",
        "fovy",       &CameraComponent::fovy,
        "nearClip",   &CameraComponent::nearClip,
        "farClip",    &CameraComponent::farClip,
        "orthoSize",  &CameraComponent::orthoSize,
        // Exposed through the enum rather than as the raw bool it is stored as,
        // so both sides of the conversation use the same vocabulary:
        //   camera.projection = Projection.Orthographic
        "projection", sol::property(
            [](const CameraComponent& c) { return c.orthographic; },
            [](CameraComponent& c, bool v) { c.orthographic = v; })
    );

    // entity:hasComponent_Camera / getComponent_Camera / addComponent_Camera
    RegisterComponentAccess<CameraComponent>(lua, "Camera");

    // Scene.createCamera([name]) -> entity, already carrying a Camera.
    //
    // A convenience over createEntity + addComponent_Camera, and worth having
    // because a camera is the one component whose whole purpose is to exist and
    // be pointed somewhere - there is no case where you want the entity without
    // it. The component comes back configured with the defaults, so a script
    // that wants a map view only has to say so.
    sol::table scn = lua["Scene"];
    scn["createCamera"] = [](sol::optional<std::string> name) -> Entity* {
        Scene* s = Scene::Current();
        if (!s) return nullptr;
        Entity* e = s->Find(s->CreateEntity(name.value_or(std::string("Camera"))));
        if (e) e->AddComponent<CameraComponent>();
        return e;
    };
}

void DescribeCameraBindings(LuaApiRegistry& api) {
    api.Table("Scene").Fn("createCamera([name]) -> entity",
                          "Create an entity that already carries a Camera");

    api.Usertype("Entity", "entity")
        .Method("hasComponent_Camera() -> bool", "Whether it has a Camera")
        .Method("getComponent_Camera() -> Camera", "Its Camera, or nil")
        .Method("addComponent_Camera() -> Camera",
                "Add a Camera and return it. Returns the existing one if there is already one");

    auto c = api.Usertype("Camera", "camera");
    c.Prop("fovy",       "Vertical field of view in degrees. Perspective only");
    c.Prop("nearClip",   "Nothing closer than this is drawn");
    c.Prop("farClip",    "Nothing further than this is drawn - the view distance");
    c.Prop("orthoSize",  "How many world units tall the view is. Orthographic only");
    c.Prop("projection", "Projection.Perspective or Projection.Orthographic");

    api.Table("Projection")
        .Value("Perspective",  "Things shrink with distance, as an eye sees them")
        .Value("Orthographic", "No perspective at all - what a map or plan view needs");
}

} // namespace eng
