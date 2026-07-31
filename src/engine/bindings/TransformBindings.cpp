#include "engine/LuaBindings.h"
#include "engine/LuaApiRegistry.h"
#include "engine/Scene.h"       // Transform3D, and Entity through it

#include "raymath.h"            // quaternion and vector maths

namespace eng {

void RegisterTransformBindings(sol::state& lua) {
    // --- Expose C++ types to Lua ------------------------------------------
    // new_usertype tells sol2 how a C++ type looks from Lua: which fields and
    // methods are reachable. After this, Lua code can read and write these
    // objects directly, e.g. `entity.transform.position.x = 5`.
    lua.new_usertype<Vector3>("Vector3",
        "x", &Vector3::x, "y", &Vector3::y, "z", &Vector3::z);

    // The rotation quaternion is exposed read-only in spirit: scripts should
    // turn things with transform:rotate (below), not by editing x/y/z/w, which
    // would break the quaternion's unit-length requirement.
    lua.new_usertype<Quaternion>("Quaternion",
        "x", &Quaternion::x, "y", &Quaternion::y,
        "z", &Quaternion::z, "w", &Quaternion::w);

    // The Transform type, plus several helper METHODS defined inline as
    // "lambdas" (anonymous functions written as [](args){ body }). Each takes
    // the Transform it is called on as its first argument.
    lua.new_usertype<Transform3D>("Transform",
        "position", &Transform3D::position,
        "rotation", &Transform3D::rotation,
        "scale",    &Transform3D::scale,

        // transform:rotate(ax, ay, az, degrees) — rotate `degrees` around the
        // local axis (ax,ay,az). It builds a small rotation quaternion and
        // multiplies it in, so calling it repeatedly (every frame) accumulates
        // cleanly without gimbal lock.
        "rotate", [](Transform3D& t, float ax, float ay, float az, float deg) {
            float len = std::sqrt(ax * ax + ay * ay + az * az);
            if (len < 1e-6f) return;                 // ignore a zero-length axis
            Quaternion dq = QuaternionFromAxisAngle(Vector3Normalize({ax, ay, az}),
                                                    deg * DEG2RAD);   // degrees -> radians
            t.rotation = QuaternionMultiply(t.rotation, dq);
        },

        // The three facing directions in WORLD space, each a unit vector.
        // "forward" is the local -Z axis rotated by the orientation; an
        // unrotated object faces -Z. Scripts use these to thrust, aim and fire.
        "forward", [](Transform3D& t) {
            return Vector3RotateByQuaternion({0.0f, 0.0f, -1.0f}, t.rotation);
        },
        "right", [](Transform3D& t) {
            return Vector3RotateByQuaternion({1.0f, 0.0f, 0.0f}, t.rotation);
        },
        "up", [](Transform3D& t) {
            return Vector3RotateByQuaternion({0.0f, 1.0f, 0.0f}, t.rotation);
        },

        // translateLocal(dx,dy,dz) — move by an offset given in the object's
        // OWN axes. The offset is rotated by the orientation first, so
        // translateLocal(0,0,-d) always means "d units forward".
        "translateLocal", [](Transform3D& t, float dx, float dy, float dz) {
            Vector3 o = Vector3RotateByQuaternion({dx, dy, dz}, t.rotation);
            t.position.x += o.x;  t.position.y += o.y;  t.position.z += o.z;
        },

        // lookAt(x,y,z) — turn so the object's forward points at a world
        // point, staying upright. Handy for cameras, turrets, homing missiles.
        "lookAt", [](Transform3D& t, float x, float y, float z) {
            float dx = x - t.position.x, dy = y - t.position.y, dz = z - t.position.z;
            if (dx * dx + dy * dy + dz * dz < 1e-8f) return;   // aimed at ourselves: skip
            // MatrixLookAt builds a "view" matrix (world seen from the eye).
            // Inverting it gives the eye's own orientation matrix, whose
            // rotation part is exactly the facing we want.
            Matrix view = MatrixLookAt(t.position, {x, y, z}, {0.0f, 1.0f, 0.0f});
            t.rotation = QuaternionFromMatrix(MatrixInvert(view));
        },
        // lookAtUp(x,y,z, ux,uy,uz) — the same aim, but with the "up"
        // direction given rather than assumed to be world up.
        //
        // This is what makes a rolling camera possible. Plain lookAt always
        // keeps world up, so the horizon it produces is always level: a camera
        // following a banking aircraft would stay stubbornly upright no matter
        // what the aircraft did. Passing the AIRCRAFT's own up axis instead
        // tilts the horizon with it, and passing something part-way between the
        // two rolls the view only partly - which is how flight games avoid
        // spinning the whole screen during a fast roll.
        //
        // It also sidesteps a failure that plain lookAt cannot avoid: aiming
        // straight up or straight down is parallel to world up, the two cross
        // to zero, and the view matrix comes out as NaN. An up axis taken from
        // the aircraft is always square to where it is pointing.
        "lookAtUp", [](Transform3D& t, float x, float y, float z,
                         float ux, float uy, float uz) {
            float dx = x - t.position.x, dy = y - t.position.y, dz = z - t.position.z;
            if (dx * dx + dy * dy + dz * dz < 1e-8f) return;   // aimed at ourselves: skip
            Vector3 up{ux, uy, uz};
            if (Vector3Length(up) < 1e-6f) up = {0.0f, 1.0f, 0.0f};
            up = Vector3Normalize(up);
            // Reject an up direction lying along the line of sight, for the
            // reason above. Keeping the previous orientation for one frame is a
            // far better failure than a frame of NaN.
            Vector3 dir = Vector3Normalize({dx, dy, dz});
            if (std::fabs(Vector3DotProduct(up, dir)) > 0.9999f) return;
            Matrix view = MatrixLookAt(t.position, {x, y, z}, up);
            t.rotation = QuaternionFromMatrix(MatrixInvert(view));
        },
        // rotateToward(x,y,z, max_degrees) — turn PART-WAY toward facing a
        // world point, by at most max_degrees this call. Unlike lookAt (which
        // snaps instantly), this gives a limited turn rate, so an AI plane
        // banks toward its target and can overshoot if it can't turn fast
        // enough. Returns having rotated as far as allowed.
        "rotateToward", [](Transform3D& t, float x, float y, float z, float maxDeg) {
            float dx = x - t.position.x, dy = y - t.position.y, dz = z - t.position.z;
            if (dx * dx + dy * dy + dz * dz < 1e-8f) return;   // target is here: skip
            // The orientation we would have if we faced the target directly.
            Matrix view = MatrixLookAt(t.position, {x, y, z}, {0.0f, 1.0f, 0.0f});
            Quaternion target = QuaternionFromMatrix(MatrixInvert(view));
            // The angle between our current orientation and that target one.
            // (For unit quaternions, the dot product's arccos, times two, is
            // the rotation angle between them.)
            float dot = t.rotation.x * target.x + t.rotation.y * target.y +
                        t.rotation.z * target.z + t.rotation.w * target.w;
            dot = std::fabs(dot);
            if (dot > 0.9995f) { t.rotation = target; return; }   // essentially aligned
            float angle  = 2.0f * std::acos(dot < 1.0f ? dot : 1.0f);   // radians
            float maxRad = maxDeg * DEG2RAD;
            // Slerp is smooth rotation interpolation; the fraction is how far of
            // the way to the target we're allowed to go this call (capped at 1).
            float frac = (angle > 0.0f) ? (maxRad / angle) : 1.0f;
            if (frac > 1.0f) frac = 1.0f;
            t.rotation = QuaternionSlerp(t.rotation, target, frac);
        });

}

void DescribeTransformBindings(LuaApiRegistry& api) {
    api.Usertype("Vector3", "v")
        .Prop("x", "The X component")
        .Prop("y", "The Y component")
        .Prop("z", "The Z component");

    api.Usertype("Quaternion", "q")
        .Prop("x", "Imaginary X. Edit through the Transform methods, not directly")
        .Prop("y", "Imaginary Y")
        .Prop("z", "Imaginary Z")
        .Prop("w", "Real part. Identity is {0,0,0,1}");

    auto t = api.Usertype("Transform", "transform");
    t.Prop("position", "Local position, a Vector3");
    t.Prop("rotation", "Local orientation, a Quaternion");
    t.Prop("scale",    "Local scale, a Vector3");
    t.Method("rotate(ax, ay, az, degrees)",
             "Turn about a LOCAL axis. Accumulates cleanly frame after frame");
    t.Method("forward() -> Vector3", "The way it faces: local -Z, in world space");
    t.Method("right() -> Vector3",   "Local +X in world space");
    t.Method("up() -> Vector3",      "Local +Y in world space");
    t.Method("translateLocal(dx, dy, dz)",
             "Move along its OWN axes, so (0,0,-d) always means d forward");
    t.Method("lookAt(x, y, z)", "Face a world point, staying upright");
    t.Method("lookAtUp(x, y, z, ux, uy, uz)",
             "Face a world point with a chosen up direction - what lets a view roll");
    t.Method("rotateToward(x, y, z, max_degrees)",
             "Turn PART-WAY toward a point, at most this many degrees. A limited turn rate");
}

} // namespace eng
