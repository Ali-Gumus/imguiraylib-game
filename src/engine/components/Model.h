#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
// Draws the entity as a loaded 3D MODEL (an .obj or .glb file) instead of a
// simple primitive. The model file is loaded lazily the first time it's drawn,
// and freed when the component is destroyed.
class ModelComponent : public Component {
public:
    ModelComponent() = default;
    // A loaded Model owns GPU resources that must be freed exactly once, so we
    // forbid copying this component (which would copy the handles and free them
    // twice). Clone() below makes a fresh, independent one instead.
    ModelComponent(const ModelComponent&) = delete;
    ModelComponent& operator=(const ModelComponent&) = delete;
    ~ModelComponent() override;

    const char* Name() const override { return "Model"; }

    // Clone copies only the path and tint; the new component loads its own copy
    // of the model on its first draw.
    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<ModelComponent>();
        c->path           = path;
        c->texture        = texture;
        c->tint           = tint;
        c->rotationOffset = rotationOffset;
        c->positionOffset = positionOffset;
        c->scale          = scale;
        return c;
    }

    void OnDraw(const Entity& owner) override;
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["path"] = path;
        out["texture"] = texture;
        out["tint"] = {tint.r, tint.g, tint.b, tint.a};
        out["rotationOffset"] = {rotationOffset.x, rotationOffset.y, rotationOffset.z};
        out["positionOffset"] = {positionOffset.x, positionOffset.y, positionOffset.z};
        out["scale"] = {scale.x, scale.y, scale.z};
    }
    void Deserialize(const nlohmann::json& in) override {
        // Read BEFORE SetPath, because SetPath is what marks the model for
        // reloading - and the texture has to be known by the time that reload
        // happens or the mesh would be cached untextured.
        texture = in.value("texture", texture);
        SetPath(in.value("path", path));
        if (in.contains("tint"))
            tint = {in["tint"][0], in["tint"][1], in["tint"][2], in["tint"][3]};
        if (in.contains("rotationOffset"))
            rotationOffset = {in["rotationOffset"][0], in["rotationOffset"][1], in["rotationOffset"][2]};
        if (in.contains("positionOffset"))
            positionOffset = {in["positionOffset"][0], in["positionOffset"][1], in["positionOffset"][2]};
        if (in.contains("scale"))
            scale = {in["scale"][0], in["scale"][1], in["scale"][2]};
    }

    // Change which file to draw (unloads any current model so the new one loads
    // on the next draw).
    void SetPath(const std::string& p);

    // Change which image is painted over the model. Reloads on the next draw,
    // because the texture is part of what the shared model cache is keyed by.
    void SetTexture(const std::string& t);

    // How many triangles this model draws, summed over all its meshes. Zero
    // until the file has actually loaded. The editor totals these to show what
    // a scene costs to render.
    int TriangleCount() const;

    // Whether the file is loaded and can be drawn.
    bool IsLoaded() const { return m_loaded; }

    // Whether loading was ATTEMPTED and did not work - a missing or unreadable
    // file. Distinct from "not loaded", which is also true of a model that has
    // simply not been drawn yet, and the distinction matters: ShapeComponent
    // uses this to decide whether to fall back to its primitive, and treating
    // "not yet" as failure would flash a cube on every model's first frame.
    bool LoadFailed() const { return m_tried && !m_loaded; }

    // Load now rather than on the next draw. Spawning uses this so that a
    // script's onStart can ask whether it ended up with a usable model - which
    // it cannot do while the load is lazy, because nothing has drawn yet.
    void ForceLoad();

    std::string path;             // the model file, e.g. "assets/models/jet.obj"

    // An image to paint over the model, e.g. "assets/models/hangar_diffuse.png".
    // Empty means "use whatever the model file brought with it".
    //
    // WHY THIS IS NEEDED AT ALL. A `.glb` normally carries its textures inside
    // it and needs nothing here. But plenty of models are distributed as a mesh
    // plus loose image files - always an `.obj` with its `.mtl`, and often a
    // `.gltf` that references images by a relative path that stopped being true
    // the moment the files were moved. When the material ends up with no image,
    // raylib draws the mesh with a plain white one, so the model appears in the
    // right place at the right size in a flat untextured colour, which looks far
    // more like a lighting problem than a missing file.
    //
    // Naming the image here binds it to every material on the model as the
    // diffuse map - the colour of the surface before lighting.
    //
    // ONE IMAGE FOR THE WHOLE MODEL. A mesh split into several materials, each
    // with its own image, cannot be described this way; that needs the model
    // file's own material data to be correct. This covers the common case of one
    // mesh and one texture sitting beside it.
    std::string texture;

    Color       tint = WHITE;     // multiplied over the model's own colors
    // A fixed rotation (euler degrees) applied to the mesh when drawing, so a
    // model authored facing a different axis can be aligned to the engine's
    // -Z forward / +Y up convention without rotating the gameplay transform.
    Vector3     rotationOffset{0, 0, 0};

    // A fixed SHIFT (in the model's own units) applied to the mesh before that
    // rotation, used to move a model onto its entity's origin.
    //
    // Every model carries a "pivot": the point its coordinates are measured
    // from, chosen by whoever built it. Engines assume that point is at the
    // middle of the object, but exported models frequently put it somewhere
    // else entirely - at the nose, at a wingtip, or at the world origin of the
    // scene the model was authored in, which can be a long way off.
    //
    // That matters far beyond looking untidy, because EVERYTHING rotates about
    // the entity's origin. With the pivot outside the aircraft, turning the
    // entity swings the model around a point in mid-air like a ball on a
    // string, instead of banking it about its own centre. The collider, which
    // is placed sensibly around the origin, then no longer covers the visible
    // aircraft either.
    //
    // Setting this to the negative of the model's centre brings it back onto
    // the origin. The Inspector's "Centre On Origin" button works that out from
    // the model's bounding box rather than leaving it to be found by dragging.
    //
    // It is applied BEFORE the rotation offset, in the model's own frame, since
    // it describes where the mesh sits inside its own coordinates - so getting
    // the centring right once keeps working whatever the alignment rotation is
    // later set to.
    Vector3     positionOffset{0, 0, 0};

    // How much to resize the MODEL, without touching the entity.
    //
    // Model files disagree wildly about what one unit means - the same
    // helicopter may arrive 100 times too big or a hundredth of the size it
    // should be. The obvious fix is to scale the entity, but that is the wrong
    // knob: an entity's scale is what the rest of the game measures against.
    // Scaling it drags along every child, and it makes the entity claim to be a
    // size it is not, so a 0.01 entity reads as a centimetre-wide object when it
    // is really a helicopter that happens to have been exported large.
    //
    // Keeping this separate means the entity stays at its true size and only
    // the drawing is adjusted - which is exactly the distinction rotationOffset
    // and positionOffset already make.
    //
    // It is applied AFTER positionOffset, so that offset stays measured in the
    // model's own raw units - the same units the bounding box and the "Centre
    // On Origin" button work in - and scaling the model scales the shift with
    // it, instead of the two disagreeing.
    Vector3     scale{1.0f, 1.0f, 1.0f};

    // The model's bounding box in its own coordinates, and whether it is known
    // yet (it cannot be, until the file has actually loaded). The Inspector
    // reports it, which is what turns "the model is in the wrong place" from a
    // guess into a number.
    bool        Bounds(Vector3& outMin, Vector3& outMax) const;

private:
    void EnsureLoaded();          // load the file the first time we need it
    Model  m_model{};             // the loaded model (raylib type)
    Matrix m_baseTransform{};     // the model's own transform, captured at load
    bool   m_loaded = false;      // did it load successfully?
    bool   m_tried  = false;      // have we already attempted to load `path`?
};
// Free every model file the engine has loaded. Model files are loaded once and
// shared by everything that draws them (see the cache note in Components.cpp),
// and are kept until this is called. Call ONCE at shutdown, while the window -
// and therefore the graphics device - still exists.
void ClearModelCache();

// Load a model file into that shared cache now, rather than when something
// first draws it. Use it to move an unavoidable cost to a moment where a pause
// is expected - pressing Play - instead of mid-game when a wave spawns.
// Does nothing if the file is already loaded or cannot be read.
void PreloadModel(const std::string& path, const std::string& texture = "");
} // namespace eng
