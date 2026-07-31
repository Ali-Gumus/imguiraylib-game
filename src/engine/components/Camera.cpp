#include "engine/components/Camera.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eng {
// ---- CameraComponent -------------------------------------------------------

Camera3D CameraComponent::ToCamera3D(const Matrix& world) const {
    // `world` already includes every parent transform. We find two points in
    // world space: where the camera sits (its local origin) and a point one
    // unit ahead of it (its local -Z, which is "forward"). Vector3Transform
    // applies the matrix to a point. Feeding the eye and a forward point to
    // raylib is enough to describe the view.
    Vector3 pos = Vector3Transform({0.0f, 0.0f, 0.0f}, world);
    Vector3 tgt = Vector3Transform({0.0f, 0.0f, -1.0f}, world);

    // The view's "up" comes from the entity's OWN up axis (local +Y), not from
    // world up. That is what lets a camera ROLL: with world up hardcoded here,
    // an entity could be banked right over and the horizon would still be drawn
    // dead level, because the roll was discarded before the view was built.
    // A chase camera that banks with an aircraft depends entirely on this.
    //
    // It is found as a DIRECTION - the local up point minus the eye - rather
    // than by transforming {0,1,0} as a point, which would include the
    // translation and give a position instead of an axis.
    Vector3 up = Vector3Subtract(Vector3Transform({0.0f, 1.0f, 0.0f}, world), pos);

    // Guard against the two ways this can degenerate, because both produce a
    // view matrix full of NaNs and a black screen rather than a wrong picture.
    //
    // 1. A zero-length up axis, from a world matrix scaled flat on Y.
    // 2. An up axis PARALLEL to the direction of view. The view matrix is built
    //    by crossing the two, and the cross product of parallel vectors is
    //    zero. This was easy to hit while up was world up - an entity pitching
    //    to point straight upwards then looked along its own up axis, which is
    //    exactly the vertical climb this game invites. Taking up from the
    //    entity's frame CANNOT do it under a pure rotation, since a rotation
    //    keeps the local axes square to each other; only a world matrix
    //    carrying scale or shear can still collapse them.
    const float upLen = Vector3Length(up);
    if (upLen < 1e-6f) {
        up = {0.0f, 1.0f, 0.0f};
    } else {
        up = Vector3Scale(up, 1.0f / upLen);
        Vector3 dir = Vector3Subtract(tgt, pos);
        const float dirLen = Vector3Length(dir);
        if (dirLen > 1e-6f) {
            dir = Vector3Scale(dir, 1.0f / dirLen);
            // Nearly parallel: fall back to the entity's own +X axis, which is
            // square to its forward by construction, so the view stays valid
            // and merely rolls to a defined orientation instead of breaking.
            if (std::fabs(Vector3DotProduct(up, dir)) > 0.9999f) {
                Vector3 side = Vector3Subtract(
                    Vector3Transform({1.0f, 0.0f, 0.0f}, world), pos);
                Vector3 fixed = Vector3CrossProduct(dir, side);
                if (Vector3Length(fixed) > 1e-6f) up = Vector3Normalize(fixed);
            }
        }
    }

    Camera3D cam{};                         // zero-initialize all fields
    cam.position   = pos;                   // where the eye is
    cam.target     = tgt;                   // the point it looks at
    cam.up         = up;                    // which way is "up" for the view
    // The graphics layer reads ONE field for both projections, and reads it as
    // a different quantity for each: an angle in degrees when perspective, and
    // a height in world units when orthographic. Keeping the two apart on the
    // component and choosing here means neither setting is destroyed by
    // switching, and nothing else has to know about the overload.
    cam.projection = orthographic ? CAMERA_ORTHOGRAPHIC : CAMERA_PERSPECTIVE;
    cam.fovy       = orthographic ? orthoSize : fovy;
    return cam;
}

void CameraComponent::OnInspector() {
    // Projection first: it decides which of the two size fields below means
    // anything, so showing it after them would invite tuning the one that is
    // currently ignored.
    const char* kProjections[] = {"Perspective", "Orthographic"};
    int proj = orthographic ? 1 : 0;
    if (ImGui::Combo("Projection", &proj, kProjections, 2)) orthographic = (proj == 1);

    if (orthographic) {
        ImGui::DragFloat("Ortho Size", &orthoSize, 1.0f, 1.0f, 200000.0f, "%.0f",
                         ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How many world units TALL the view is.\n"
                              "An orthographic camera has no angle - everything\n"
                              "is drawn the same size however far away it is.");
        ImGui::TextDisabled("FOV is unused while orthographic");
    } else {
        ImGui::DragFloat("FOV", &fovy, 0.5f, 10.0f, 140.0f);
    }

    // Clipping planes. Far is the view distance, so it is dragged on a
    // LOGARITHMIC scale: the useful range runs from a few hundred units for an
    // indoor scene to tens of thousands for open terrain, and a linear drag
    // across that span would step in useless increments at one end or the other.
    ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.01f, 100.0f, "%.2f");
    ImGui::DragFloat("Far Clip",  &farClip,  10.0f,  1.0f,  200000.0f, "%.0f",
                     ImGuiSliderFlags_Logarithmic);

    // The near plane must stay in front of the far plane. Dragging near upwards
    // past far would otherwise produce an inside-out frustum and a black view,
    // which looks like a crash rather than a bad number.
    if (nearClip >= farClip) nearClip = farClip * 0.5f;

    // Warn when the two are far enough apart to cause z-fighting. The ratio is
    // what matters (see the header), and past roughly 100000:1 distant surfaces
    // begin to flicker against each other. Saying so here, next to the control
    // that causes it, is what stops it being diagnosed as a mystery later.
    if (nearClip > 0.0f && (farClip / nearClip) > 100000.0f) {
        ImGui::TextColored({1.0f, 0.8f, 0.3f, 1.0f},
                           "far/near = %.0f:1 - distant surfaces may flicker",
                           farClip / nearClip);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Raise Near Clip rather than lowering Far Clip.\n"
                              "Depth precision depends on the ratio, and a chase\n"
                              "camera never has anything within a metre of it.");
    }

    // TextDisabled draws greyed-out helper text.
    ImGui::TextDisabled("position/rotation come from Transform");
}

} // namespace eng
