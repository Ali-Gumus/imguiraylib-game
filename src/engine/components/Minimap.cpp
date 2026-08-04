#include "engine/components/Minimap.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "engine/components/Terrain.h"
#include "engine/Components.h"
#include "rlgl.h"

namespace eng {
// ---- MinimapComponent ------------------------------------------------------

void MinimapComponent::OnInspector() {
    // Range on a logarithmic drag: a dogfight radar is a couple of kilometres
    // and a navigation view is tens, which a linear drag serves badly at one end.
    ImGui::DragFloat("Range (m)", &range, 10.0f, 100.0f, 100000.0f, "%.0f",
                     ImGuiSliderFlags_Logarithmic);
    ImGui::DragInt("Size (px)", &size, 1.0f, 64, 600);

    const char* corners[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    ImGui::Combo("Corner", &corner, corners, 4);

    ImGui::Checkbox("Terrain backdrop", &showTerrain);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bakes the landscape into a small image once, from the\n"
                          "same heights the terrain mesh uses. Costs nothing per\n"
                          "frame; turn it off for a plain contacts-only radar.");

    char buf[64];
    std::strncpy(buf, blipTag.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Contact tag", buf, sizeof(buf))) blipTag = buf;

    char tbuf[64];
    std::strncpy(tbuf, targetTag.c_str(), sizeof(tbuf) - 1);
    tbuf[sizeof(tbuf) - 1] = '\0';
    if (ImGui::InputText("Target tag", tbuf, sizeof(tbuf))) targetTag = tbuf;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drawn as an amber ring with cross-hairs instead of a\n"
                          "dot: a place to attack rather than a contact to avoid.\n"
                          "Leave empty to turn it off.");
    ImGui::DragFloat("Target size", &targetSize, 0.25f, 2.0f, 30.0f, "%.1f");

    ImGui::TextDisabled("centred on this entity, heading-up");
    // Re-bake if the terrain has changed underneath us. The image is built once
    // and kept, so regenerating the landscape would otherwise leave the radar
    // showing the old hills for the rest of the session.
    if (ImGui::Button("Rebake Terrain")) {
        if (m_built) UnloadTexture(m_terrain);
        m_built = false;
        m_tried = false;
    }
}

MinimapComponent::~MinimapComponent() {
    if (m_built) UnloadTexture(m_terrain);
}

std::unique_ptr<Component> MinimapComponent::Clone() const {
    auto c = std::make_unique<MinimapComponent>();
    c->range = range;   c->size    = size;
    c->corner = corner; c->showTerrain = showTerrain;
    c->blipTag = blipTag;
    c->targetTag = targetTag; c->targetSize = targetSize;
    // Deliberately NOT copying the baked texture; the clone bakes its own.
    return c;
}

bool MinimapComponent::EnsureTerrain() {
    if (m_built) return true;
    if (m_tried) return false;      // already failed once; don't retry every frame
    m_tried = true;

    Scene* scene = Scene::Current();
    if (!scene) return false;

    // Find the landscape and where it sits. The terrain mesh is centred on its
    // own entity, so the entity's world position is the middle of the map.
    const TerrainComponent* terrain = nullptr;
    for (const Entity& e : scene->Entities()) {
        for (const auto& comp : e.components) {
            if (auto* t = dynamic_cast<const TerrainComponent*>(comp.get())) {
                terrain = t;
                m_worldCentre = Vector3Transform({0.0f, 0.0f, 0.0f},
                                                 scene->WorldMatrix(e, true));
                break;
            }
        }
        if (terrain) break;
    }
    if (!terrain) return false;

    m_worldSize = terrain->worldSize;
    if (m_worldSize <= 0.0f) return false;

    // Sample the same heights the mesh and the collision surface are built from,
    // so the radar cannot show hills that are not there. A modest grid is plenty:
    // this image is only ever drawn a couple of hundred pixels across.
    const int n = 256;
    const std::vector<float> h = terrain->SampleHeights(n);
    if (h.empty()) return false;

    // HILLSHADE: colour each pixel by how the ground faces an imagined light,
    // rather than by height alone. Height alone gives a flat wash where the
    // ground is high but level; shading the SLOPE is what makes ridges and
    // valleys legible at this size. The light comes from the north-west, the
    // cartographic convention - lit from the other side and hills read as pits.
    Image img = GenImageColor(n, n, Color{20, 28, 22, 255});
    Color* px = LoadImageColors(img);

    // How much to exaggerate the slope. The grid step in metres is worldSize/n,
    // and heights arrive as 0..1, so this converts a height difference into a
    // comparable horizontal one. Terrain read at map scale is nearly flat, so it
    // is deliberately overstated - a truthful gradient would be invisible.
    const float step  = m_worldSize / (float)n;
    const float relief = (terrain->maxHeight > 0.0f) ? terrain->maxHeight : 1.0f;
    const float scale = (relief / step) * 12.0f;

    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            // Central differences, clamped at the edges.
            const int xm = (x > 0) ? x - 1 : x, xp = (x + 1 < n) ? x + 1 : x;
            const int zm = (z > 0) ? z - 1 : z, zp = (z + 1 < n) ? z + 1 : z;
            const float dx = (h[(size_t)z * n + xp] - h[(size_t)z * n + xm]) * scale;
            const float dz = (h[(size_t)zp * n + x] - h[(size_t)zm * n + x]) * scale;

            // The surface normal of a heightfield is (-dh/dx, 1, -dh/dz).
            Vector3 nrm = Vector3Normalize({-dx, 1.0f, -dz});
            const Vector3 light = Vector3Normalize({-0.6f, 0.7f, -0.4f});
            float lit = Vector3DotProduct(nrm, light);
            if (lit < 0.0f) lit = 0.0f;

            // Mix the shading with a little of the raw height, so high ground
            // still reads as high on an evenly-lit slope.
            const float hgt = h[(size_t)z * n + x];
            float t = 0.25f + lit * 0.65f + hgt * 0.25f;
            if (t > 1.0f) t = 1.0f;

            // Dark blue-green in the hollows to a pale sunlit green on the tops.
            const Color lo{18, 34, 26, 255}, hi{132, 168, 116, 255};
            px[(size_t)z * n + x] = Color{
                (unsigned char)(lo.r + (hi.r - lo.r) * t),
                (unsigned char)(lo.g + (hi.g - lo.g) * t),
                (unsigned char)(lo.b + (hi.b - lo.b) * t),
                255};
        }
    }

    // Hand the pixels back to the image, then to the graphics card.
    UnloadImage(img);
    img = Image{px, n, n, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    m_terrain = LoadTextureFromImage(img);
    UnloadImage(img);                       // frees px

    // CLAMP, not repeat. The radar reads a window around the aircraft, and near
    // the edge of the world that window runs off the image; repeating would tile
    // the far side of the map into view as though the world continued.
    SetTextureWrap(m_terrain, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(m_terrain, TEXTURE_FILTER_BILINEAR);
    m_built = true;
    return true;
}

void MinimapComponent::OnDrawHud(const Entity& owner, int width, int height) {
    Scene* scene = Scene::Current();
    if (!scene || size < 32 || range <= 1.0f) return;

    const int   d  = size;
    const float hd = d * 0.5f;
    const float m  = 16.0f;                      // margin from the screen edge

    // Centre of the radar, placed in the chosen corner.
    float cx = 0.0f, cy = 0.0f;
    switch (corner) {
        case 0:  cx = m + hd;                cy = m + hd;                 break;
        case 1:  cx = (float)width - m - hd; cy = m + hd;                 break;
        case 2:  cx = m + hd;                cy = (float)height - m - hd; break;
        default: cx = (float)width - m - hd; cy = (float)height - m - hd; break;
    }

    // Where the owner is and which way it faces, taken from the WORLD matrix so
    // that a radar on a parented entity still reads true.
    const Matrix w = scene->WorldMatrix(owner, true);
    const Vector3 pos = Vector3Transform({0.0f, 0.0f, 0.0f}, w);
    Vector3 fwd = Vector3Subtract(Vector3Transform({0.0f, 0.0f, -1.0f}, w), pos);
    fwd.y = 0.0f;                                // heading only: ignore climb
    float fl = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
    if (fl < 1e-5f) { fwd = {0.0f, 0.0f, -1.0f}; fl = 1.0f; }
    fwd.x /= fl; fwd.z /= fl;

    // The angle to turn the world by so the nose points up the screen. On a
    // top-down map +X is right and +Z is down, so "up" is (0,-1); the heading is
    // measured from there and the map is turned back by it.
    const float heading = atan2f(fwd.x, -fwd.z);
    const float ca = cosf(-heading), sa = sinf(-heading);

    // World offset -> radar pixel, heading-up.
    const float ppm = hd / range;                // pixels per metre
    auto project = [&](Vector3 p, float& sx, float& sy) {
        const float ox = (p.x - pos.x) * ppm;
        const float oz = (p.z - pos.z) * ppm;
        sx = cx + (ox * ca - oz * sa);
        sy = cy + (ox * sa + oz * ca);
    };

    // --- Backdrop -----------------------------------------------------------
    DrawRectangle((int)(cx - hd), (int)(cy - hd), d, d, Color{8, 14, 12, 190});

    if (showTerrain && EnsureTerrain()) {
        // Clip to the radar square. rlScissor is used rather than raylib's
        // BeginScissorMode because that helper flips Y using the WINDOW height,
        // and this is being drawn into the game view's own texture, which is a
        // different size - the clip would land somewhere else entirely.
        rlDrawRenderBatchActive();               // flush before changing GL state
        rlEnableScissorTest();
        rlScissor((int)(cx - hd), height - (int)(cy + hd), d, d);

        // The source window has to cover the DIAGONAL of the radar square, or
        // rotating it would swing empty texture into the corners.
        const float diag   = 1.41422f;
        const float texel  = (float)m_terrain.width / m_worldSize;   // pixels per metre
        const float srcHalf = range * diag * texel;
        const float u = (pos.x - (m_worldCentre.x - m_worldSize * 0.5f)) * texel;
        const float v = (pos.z - (m_worldCentre.z - m_worldSize * 0.5f)) * texel;

        const Rectangle src{u - srcHalf, v - srcHalf, srcHalf * 2.0f, srcHalf * 2.0f};
        const float dw = d * diag;
        const Rectangle dst{cx, cy, dw, dw};
        DrawTexturePro(m_terrain, src, dst, {dw * 0.5f, dw * 0.5f},
                       -heading * RAD2DEG, WHITE);

        rlDrawRenderBatchActive();               // flush while the clip still applies
        rlDisableScissorTest();
    }

    // --- Contacts -----------------------------------------------------------
    const Color hud{90, 255, 130, 220};
    for (const Entity& e : scene->Entities()) {
        if (e.tag != blipTag) continue;
        const Vector3 ep = Vector3Transform({0.0f, 0.0f, 0.0f},
                                            scene->WorldMatrix(e, true));
        float sx, sy;
        project(ep, sx, sy);

        // Hold anything beyond the rim ON the rim, hollowed out, so a threat is
        // never simply absent - you still know the bearing, just not the range.
        const float dx = sx - cx, dy = sy - cy;
        const float dist = sqrtf(dx * dx + dy * dy);
        const bool  out  = dist > hd - 4.0f;
        if (out && dist > 0.001f) {
            const float k = (hd - 4.0f) / dist;
            sx = cx + dx * k;  sy = cy + dy * k;
        }
        if (out) DrawCircleLines((int)sx, (int)sy, 3.0f, Color{255, 110, 90, 230});
        else     DrawCircle((int)sx, (int)sy, 3.0f, Color{255, 90, 70, 240});
    }

    // --- Target areas -------------------------------------------------------
    // Drawn AFTER the contacts so an objective is never hidden under a blip,
    // and as a ring with a cross through it rather than a filled dot: it marks
    // a place on the ground to go to, which is the opposite of a moving contact
    // to stay away from. Amber, because it is neither friendly nor a threat.
    if (!targetTag.empty()) {
        const Color mark{255, 200, 80, 235};
        for (const Entity& e : scene->Entities()) {
            if (e.tag != targetTag) continue;
            const Vector3 ep = Vector3Transform({0.0f, 0.0f, 0.0f},
                                                scene->WorldMatrix(e, true));
            float sx, sy;
            project(ep, sx, sy);

            // Held on the rim like a contact, for the same reason: an objective
            // off the edge of the radar should still give its bearing.
            const float dx = sx - cx, dy = sy - cy;
            const float dist = sqrtf(dx * dx + dy * dy);
            const float lim  = hd - targetSize - 2.0f;
            if (dist > lim && dist > 0.001f) {
                const float k = lim / dist;
                sx = cx + dx * k;  sy = cy + dy * k;
            }

            DrawCircleLines((int)sx, (int)sy, targetSize, mark);
            // The cross-hairs, which is what turns a circle into a target mark.
            DrawLine((int)(sx - targetSize - 3.0f), (int)sy,
                     (int)(sx - targetSize + 1.0f), (int)sy, mark);
            DrawLine((int)(sx + targetSize - 1.0f), (int)sy,
                     (int)(sx + targetSize + 3.0f), (int)sy, mark);
            DrawLine((int)sx, (int)(sy - targetSize - 3.0f),
                     (int)sx, (int)(sy - targetSize + 1.0f), mark);
            DrawLine((int)sx, (int)(sy + targetSize - 1.0f),
                     (int)sx, (int)(sy + targetSize + 3.0f), mark);
        }
    }

    // --- The aircraft itself, always dead centre and pointing up ------------
    DrawTriangle({cx, cy - 6.0f}, {cx - 4.5f, cy + 5.0f}, {cx + 4.5f, cy + 5.0f}, hud);

    // --- Frame, and where north went ---------------------------------------
    DrawRectangleLines((int)(cx - hd), (int)(cy - hd), d, d, hud);
    // Heading-up costs you north, so mark it: north is world -Z, turned by the
    // same angle as everything else.
    const float nx = cx + (0.0f * ca - (-(hd - 10.0f)) * sa);
    const float ny = cy + (0.0f * sa + (-(hd - 10.0f)) * ca);
    DrawCircle((int)nx, (int)ny, 2.0f, Color{200, 220, 255, 200});

    // Range readout, in kilometres - the number that makes a blip mean something.
    DrawText(TextFormat("%.0fkm", range / 1000.0f),
             (int)(cx - hd) + 4, (int)(cy + hd) - 16, 12, hud);
}

} // namespace eng
