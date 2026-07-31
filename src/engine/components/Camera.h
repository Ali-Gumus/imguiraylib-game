#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
// ============================================================================
// CameraComponent: makes its entity act as a camera. The Game view renders
// the world through it. Because the camera's position and orientation come
// from the entity's transform, anything that moves the entity (a script, a
// parent) also moves the view.
// ============================================================================
class CameraComponent : public Component {
public:
    const char* Name() const override { return "Camera"; }
    std::unique_ptr<Component> Clone() const override {
        return std::make_unique<CameraComponent>(*this);
    }
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["fovy"] = fovy;
        out["nearClip"] = nearClip;  out["farClip"] = farClip;
    }
    void Deserialize(const nlohmann::json& in) override {
        fovy     = in.value("fovy", fovy);
        // Scenes saved before clipping planes existed carry neither key, so they
        // fall back to the defaults below and keep rendering as they always did.
        nearClip = in.value("nearClip", nearClip);
        farClip  = in.value("farClip",  farClip);
    }

    // Turn an entity's world matrix (from Scene::WorldMatrix) into the
    // Camera3D struct that raylib needs to render a 3D view. Doing it from the
    // world matrix is what lets a camera parented to the jet follow it.
    Camera3D ToCamera3D(const Matrix& world) const;

    float fovy = 60.0f;         // vertical field of view in degrees (zoom)

    // ---- Clipping planes: the near and far edges of what this camera can see.
    //
    // A perspective camera does not see an infinite space. It sees a truncated
    // pyramid - a "frustum" - and these two distances are its front and back
    // faces. Anything nearer than `nearClip` or further than `farClip` is
    // discarded before it is ever drawn. `farClip` is therefore the VIEW
    // DISTANCE: with terrain tens of thousands of units across, a far plane left
    // at a few thousand means most of the landscape simply never appears, and it
    // looks like the world ends in mid-air.
    //
    // The obvious move is to push the far plane out as far as possible, but that
    // is not free, and the reason is the DEPTH BUFFER. To decide which surface is
    // in front, the GPU stores a depth per pixel in a fixed number of bits. That
    // value is not spread evenly through the frustum: it is bunched up close to
    // the camera and stretched thin far away. How badly it is stretched depends
    // almost entirely on the RATIO far/near - not on either number alone. Once
    // two distant surfaces fall within the same depth step, the GPU can no longer
    // tell which is in front, and they flicker against each other as the camera
    // moves. That flicker is called Z-FIGHTING.
    //
    // The lever that fixes it is the NEAR plane, not the far one. Doubling the
    // far distance costs the same precision as halving the near distance buys
    // back, so a camera that never gets within a metre of anything - a chase
    // camera behind an aircraft, say - should push its near plane out and spend
    // the ratio on distance instead. That is exactly the trade the defaults make:
    // 0.3 to 25000 is the same ratio as the graphics library's own 0.05 to 4000
    // default, so the precision is no worse than before while seeing six times
    // further.
    //
    // Raise `nearClip` further (1-5) if distant hills flicker; lower it if a
    // close-up object gets sliced open by the front of the frustum.
    float nearClip = 0.3f;      // nothing closer than this is drawn
    float farClip  = 25000.0f;  // nothing further than this is drawn
};
} // namespace eng
