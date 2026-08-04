#include "engine/components/Model.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "engine/Lighting.h"
#include "engine/FileDialog.h"
#include <unordered_map>

namespace eng {
// ---- ModelComponent --------------------------------------------------------

// Every distinct model FILE is loaded once and shared by everything that draws
// it.
//
// Without this, each entity loads its own copy: a wave of five helicopters
// meant five separate reads of the same six-megabyte file, five parses and five
// uploads to the graphics card, all on the single frame they first appeared -
// which stalls the game visibly at exactly the moment a wave arrives.
//
// Sharing is safe because a raylib `Model` is a small handle struct pointing at
// meshes and materials on the GPU. A component keeps its own COPY of that
// struct, so it can set its own draw transform, while the buffers underneath
// are shared. Only the transform is written per component, and it is written
// immediately before drawing, so no two entities can disagree about it.
//
// Nothing is unloaded until shutdown, deliberately. Releasing a model the
// moment its last user died would mean re-reading six megabytes the next time a
// wave spawned - the very stall this exists to remove. The cost is bounded by
// how many distinct model files a project has, which is small.
namespace {

struct CachedModel {
    Model model{};
    bool  ok = false;      // did it actually load?
};

std::unordered_map<std::string, CachedModel> g_modelCache;

// Textures are cached separately and by their own path, because the same image
// is often shared by several different models - a tileset, or one skin used by
// a family of vehicles - and uploading it once per model would waste memory for
// no reason.
std::unordered_map<std::string, Texture2D> g_textureCache;

// Load an image once and hand back the same one afterwards. A missing file is
// remembered as a zero texture so it is not retried every frame.
Texture2D AcquireTexture(const std::string& path) {
    auto it = g_textureCache.find(path);
    if (it != g_textureCache.end()) return it->second;

    Texture2D t = LoadTexture(path.c_str());
    // id 0 means the load failed. Kept in the map anyway, as the record that
    // this path has already been tried.
    g_textureCache.emplace(path, t);
    return t;
}

// Load `path` if it has not been loaded before, and return it. Returns nullptr
// if the file is missing or unreadable.
//
// THE CACHE IS KEYED BY THE MESH *AND* THE TEXTURE, not by the mesh alone.
// Materials belong to the shared model, so painting an image onto one is not a
// per-entity act: it changes what every entity using that file draws. Two
// definitions naming the same mesh with different images therefore have to be
// two cache entries, or whichever loaded last would silently retexture the
// other. The cost is one extra copy of a mesh that is genuinely being used two
// ways, and the common case - same mesh, same image - still shares.
const CachedModel* AcquireModel(const std::string& path,
                                const std::string& texture) {
    // '\n' cannot occur in either path, so it cannot make two different pairs
    // collide into one key.
    const std::string key = path + "\n" + texture;

    auto it = g_modelCache.find(key);
    if (it != g_modelCache.end())
        return it->second.ok ? &it->second : nullptr;

    CachedModel c;
    c.model = LoadModel(path.c_str());
    // LoadModel returns a model with zero meshes if the file was missing or
    // invalid; treat that as "not loaded" so we don't try to draw nothing.
    c.ok = (c.model.meshCount > 0);

    if (c.ok && !texture.empty()) {
        Texture2D tex = AcquireTexture(texture);
        if (tex.id != 0) {
            // Bind it to EVERY material as the diffuse map - the surface colour
            // before any lighting is applied. Every material, because a mesh
            // that arrived without textures usually has one material per part
            // and all of them are equally blank; texturing only the first would
            // leave a half-painted model.
            for (int i = 0; i < c.model.materialCount; ++i)
                SetMaterialTexture(&c.model.materials[i], MATERIAL_MAP_DIFFUSE, tex);
        }
    }

    if (c.ok) ApplyLightingShader(c.model);   // shade it like everything else

    // A failed load is remembered too, so a missing file is not retried on
    // every single frame for the rest of the run.
    auto res = g_modelCache.emplace(key, c);
    return res.first->second.ok ? &res.first->second : nullptr;
}

} // anonymous namespace

void PreloadModel(const std::string& path, const std::string& texture) {
    if (!path.empty()) AcquireModel(path, texture);
}

void ClearModelCache() {
    for (auto& kv : g_modelCache)
        if (kv.second.ok) UnloadModel(kv.second.model);
    g_modelCache.clear();
}

ModelComponent::~ModelComponent() {
    // Nothing to free: the model belongs to the shared cache, not to this
    // component. Unloading here would pull the mesh out from under every other
    // entity drawing the same file.
}

void ModelComponent::SetPath(const std::string& p) {
    path     = p;
    m_loaded = false;
    m_tried  = false;                     // pick up the new one on the next draw
}

void ModelComponent::SetTexture(const std::string& t) {
    texture  = t;
    // Same reset as SetPath, and for the same reason: the texture is part of
    // what the cache is keyed by, so a different one is a different entry and
    // has to be fetched again. Assigning the field alone would change what this
    // component SAYS it draws without changing what it draws.
    m_loaded = false;
    m_tried  = false;
}

void ModelComponent::ForceLoad() { EnsureLoaded(); }

void ModelComponent::EnsureLoaded() {
    if (m_tried) return;                  // only attempt the load once per path
    m_tried = true;
    if (path.empty()) return;

    const CachedModel* c = AcquireModel(path, texture);
    if (c == nullptr) return;             // missing file: stays unloaded

    // A copy of the handle struct, so this component can set its own draw
    // transform without disturbing anyone else drawing the same file.
    m_model         = c->model;
    m_baseTransform = c->model.transform;  // the file's own transform
    m_loaded        = true;
}

void ModelComponent::OnDraw(const Entity& owner) {
    EnsureLoaded();
    if (!m_loaded) return;
    // Scene::Draw already applied this entity's world matrix, so we draw at the
    // origin, unscaled. The rotation offset is folded into the model's own
    // transform so an oddly-authored model can be turned to face -Z; a model
    // that was already correct keeps offset {0,0,0} and is unchanged.
    Matrix rotate = MatrixRotateXYZ({rotationOffset.x * DEG2RAD,
                                     rotationOffset.y * DEG2RAD,
                                     rotationOffset.z * DEG2RAD});

    // The shift comes FIRST, then the rotation. Order matters here and getting
    // it the other way round is a subtle trap: the shift describes where the
    // mesh sits within its own coordinates, so it belongs in the model's frame,
    // before anything turns that frame. Applied afterwards it would be measured
    // in the rotated frame instead, and every change to the alignment rotation
    // would fling a correctly-centred model back off its origin.
    Matrix shift = MatrixTranslate(positionOffset.x, positionOffset.y,
                                   positionOffset.z);

    // Resizing the MODEL rather than the entity, so the entity keeps its true
    // size. It goes after the shift so that the shift stays measured in the
    // model's own raw units - the same units the bounding box and the "Centre
    // On Origin" button work in - and shrinks along with the model instead of
    // the two disagreeing.
    Matrix resize = MatrixScale(scale.x, scale.y, scale.z);

    // raylib multiplies in the order the transforms apply, so this reads left
    // to right: the model's own transform, the shift, the resize, the turn.
    m_model.transform = MatrixMultiply(
        MatrixMultiply(MatrixMultiply(m_baseTransform, shift), resize), rotate);
    DrawModel(m_model, {0, 0, 0}, 1.0f, tint);
}

bool ModelComponent::Bounds(Vector3& outMin, Vector3& outMax) const {
    if (!m_loaded) return false;
    // GetModelBoundingBox measures the mesh in the model's OWN coordinates,
    // which is exactly the frame positionOffset is expressed in - so the two
    // can be compared and subtracted directly.
    BoundingBox bb = GetModelBoundingBox(m_model);
    outMin = bb.min;
    outMax = bb.max;
    return true;
}

int ModelComponent::TriangleCount() const {
    if (!m_loaded) return 0;
    int total = 0;
    for (int i = 0; i < m_model.meshCount; i++)
        total += m_model.meshes[i].triangleCount;
    return total;
}

void ModelComponent::OnInspector() {
    char buf[256];
    strncpy(buf, path.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Path", buf, sizeof(buf)))
        SetPath(buf);

    if (ImGui::Button("Browse...")) {
        std::string picked = OpenFileDialog(
            "Models (*.obj *.glb *.gltf)\0*.obj;*.glb;*.gltf\0All files\0*.*\0", "obj");
        if (!picked.empty()) SetPath(picked);
    }
    ImGui::SameLine();
    if (m_loaded)              ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "loaded");
    else if (!path.empty())    ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "not found");
    else                       ImGui::TextDisabled("no model");

    // The texture, for models that arrived as a mesh plus loose image files
    // rather than with their images packed inside.
    char tbuf[256];
    strncpy(tbuf, texture.c_str(), sizeof(tbuf) - 1);
    tbuf[sizeof(tbuf) - 1] = '\0';
    if (ImGui::InputText("Texture", tbuf, sizeof(tbuf)))
        SetTexture(tbuf);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("An image painted over every material on the model.\n"
                          "Leave empty for a model that carries its own - a .glb\n"
                          "usually does. Needed for an .obj whose .mtl points at\n"
                          "images that are no longer where it expects them.");

    if (ImGui::Button("Browse texture...")) {
        std::string picked = OpenFileDialog(
            "Images (*.png *.jpg *.tga *.bmp)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0"
            "All files\0*.*\0", "png");
        if (!picked.empty()) SetTexture(picked);
    }
    ImGui::SameLine();
    if (texture.empty()) ImGui::TextDisabled("model's own");
    else                 ImGui::TextDisabled("overriding the model's own");

    // Tint color (float 0..1 in ImGui, byte 0..255 in raylib Color).
    float col[4] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", col))
        tint = {(unsigned char)(col[0] * 255), (unsigned char)(col[1] * 255),
                (unsigned char)(col[2] * 255), (unsigned char)(col[3] * 255)};

    // A fixed rotation to align the model with -Z forward / +Y up. Use this when
    // an imported model flies sideways or upside-down: a common fix is 90 or 180
    // on Y (turn it about the vertical) or -90 on X (for a Z-up model).
    float rot[3] = {rotationOffset.x, rotationOffset.y, rotationOffset.z};
    if (ImGui::DragFloat3("Rotation offset", rot, 1.0f))
        rotationOffset = {rot[0], rot[1], rot[2]};
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Turns the MODEL only, not the entity, to match this\n"
                          "engine's convention of -Z forward and +Y up.\n\n"
                          "Sideways: Y = 90 or -90.  Backwards: Y = 180.\n"
                          "Lying on its back: X = -90 (a Z-up model).\n\n"
                          "Use this rather than the entity's own Rotation,\n"
                          "which gameplay scripts overwrite every frame.");

    // Resizes the MODEL only. Dragging all three together is the usual case,
    // so a uniform field is offered first and the per-axis one below it.
    float uniform = scale.x;
    if (ImGui::DragFloat("Model scale", &uniform, 0.005f, 0.0001f, 1000.0f,
                         "%.4f"))
        scale = {uniform, uniform, uniform};
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Resizes the MODEL, leaving the entity's own scale\n"
                          "alone. Model files disagree wildly about what one\n"
                          "unit means, and scaling the ENTITY to compensate\n"
                          "drags every child with it and makes the entity\n"
                          "claim a size it does not have.");
    if (scale.x != scale.y || scale.y != scale.z)
        ImGui::DragFloat3("Scale XYZ", &scale.x, 0.005f, 0.0001f, 1000.0f, "%.4f");

    // Where the mesh sits inside its own coordinates. See the long note on
    // positionOffset for why an off-centre pivot matters so much.
    float pos[3] = {positionOffset.x, positionOffset.y, positionOffset.z};
    if (ImGui::DragFloat3("Position offset", pos, 0.05f))
        positionOffset = {pos[0], pos[1], pos[2]};
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Moves the MODEL only, to bring it onto the entity's\n"
                          "origin when its pivot was authored somewhere else.\n\n"
                          "This matters because everything rotates about the\n"
                          "origin: with the pivot outside the aircraft, turning\n"
                          "swings the model around a point in mid-air instead\n"
                          "of banking it about its own centre.");

    // Work the centring out instead of leaving it to be found by dragging.
    Vector3 mn, mx;
    if (Bounds(mn, mx)) {
        const Vector3 centre = {(mn.x + mx.x) * 0.5f,
                                (mn.y + mx.y) * 0.5f,
                                (mn.z + mx.z) * 0.5f};

        if (ImGui::Button("Centre On Origin")) {
            // Shift by the OPPOSITE of where the model's middle currently is,
            // which lands that middle exactly on the origin.
            positionOffset = {-centre.x, -centre.y, -centre.z};
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Set the position offset so the model's bounding\n"
                              "box is centred on the entity's origin.");
        ImGui::SameLine();
        if (ImGui::Button("Reset##modeloffset"))
            positionOffset = {0.0f, 0.0f, 0.0f};

        // The numbers behind the button, so a model that is still wrong can be
        // reasoned about rather than nudged blindly. The size is also the
        // quickest way to choose a collider that matches the aircraft.
        ImGui::TextDisabled("size  %.2f x %.2f x %.2f",
                            mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
        // How far the pivot is from the middle, as drawn right now. Near zero
        // means the model is centred; a large value is the thing that makes an
        // aircraft orbit a point off in space.
        const Vector3 off = {centre.x + positionOffset.x,
                             centre.y + positionOffset.y,
                             centre.z + positionOffset.z};
        const float   dist = std::sqrt(off.x * off.x + off.y * off.y +
                                       off.z * off.z);
        const Vector3 size = {mx.x - mn.x, mx.y - mn.y, mx.z - mn.z};
        const float   span = std::max({size.x, size.y, size.z});
        // Flag it only when the pivot is off by enough to be felt - a tenth of
        // the model's own size is about where a turn starts to look like a
        // swing rather than a roll.
        if (span > 0.0001f && dist > span * 0.1f)
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                               "pivot is %.2f off centre", dist);
        else
            ImGui::TextDisabled("pivot is %.2f off centre", dist);
    }
}

} // namespace eng
