#include "engine/components/Terrain.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "engine/Lighting.h"
// TerrainCollisionGrid: the power-of-two rule the collision surface obeys,
// single-sourced there so the Inspector cannot describe it differently.
#include "engine/Physics.h"

namespace eng {
// ---- TerrainComponent ------------------------------------------------------

TerrainComponent::~TerrainComponent() {
    if (m_built) UnloadModel(m_model);
}

void TerrainComponent::Rebuild() {
    if (m_built) UnloadModel(m_model);
    m_built = false;
    m_tried = false;   // regenerate on the next draw
}

// Turn a grid of heights into the terrain's triangle mesh.
//
// WHY THIS EXISTS INSTEAD OF raylib's GenMeshHeightmap
// ----------------------------------------------------
// A grid of heights is not a surface on its own. Each square of four
// neighbouring samples has to be split into two triangles, and there are two
// ways to split a square: along one diagonal or along the other. The four
// corners of a square almost never lie in the same plane, so the two choices
// describe genuinely DIFFERENT surfaces, which meet only at the square's edges
// and diverge in the middle. The gap between them at the centre of a square is
//
//     | (h00 + h11) - (h01 + h10) | / 2   (times the terrain's height)
//
// which is zero where the four corners happen to be flat and largest where the
// square is "twisted" - saddles, the shoulder of a ridge, a valley junction.
//
// That matters because the collision surface is built by the physics engine's
// heightfield, from the same grid, and IT splits every square along the
// diagonal from (x,z) to (x+1,z+1). raylib's generator splits along the other
// one, from (x,z+1) to (x+1,z). Using them together produces a landscape whose
// collision sits above the visible ground in some places and below it in
// others - so an aircraft clips through a hillside that is plainly drawn, at
// scattered spots that look arbitrary because they follow the twist of the
// noise rather than anything visible.
//
// The physics engine's choice of diagonal is fixed and internal, so the mesh is
// the side that has to agree. This generator therefore splits each square from
// (x,z) to (x+1,z+1), matching it exactly.
//
// `heights` holds n*n samples running 0..1, laid out row by row so the sample
// for grid position (x,z) is at index z*n + x - the same layout and the same
// range the collision surface is handed.
static Mesh BuildHeightfieldMesh(const std::vector<float>& heights, int n,
                                 float worldSize, float maxHeight) {
    Mesh mesh = {0};
    const int cells = n - 1;                 // squares across, one fewer than samples

    // The mesh is NOT indexed: every triangle carries its own three vertices,
    // matching what raylib's own generator produced. It costs three times the
    // vertices an indexed mesh would, and is kept only because the terrain also
    // wants flat (per-face) shading, which needs a separate normal per face and
    // so cannot share vertices between faces anyway.
    mesh.triangleCount = cells * cells * 2;
    mesh.vertexCount   = mesh.triangleCount * 3;
    mesh.vertices  = (float*)RL_MALLOC((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.normals   = (float*)RL_MALLOC((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC((size_t)mesh.vertexCount * 2 * sizeof(float));

    // Spacing between neighbouring samples: the full span divided by the number
    // of GAPS, which is one less than the number of samples.
    const float step = worldSize / (float)cells;

    // Where one grid sample sits in the mesh's own space. The mesh starts at a
    // corner rather than at its middle; OnDraw shifts it by half its width so
    // that it ends up centred on its entity, and the collision surface applies
    // the identical shift.
    auto corner = [&](int x, int z) -> Vector3 {
        return Vector3{(float)x * step,
                       heights[(size_t)z * n + x] * maxHeight,
                       (float)z * step};
    };

    int vi = 0, ni = 0, ti = 0;

    // Write one triangle: three positions, three copies of its face normal, and
    // three texture coordinates. The normal is the same for all three vertices,
    // which is what produces FLAT shading - each triangle reads as its own
    // facet, and with the contour-line overlay that is what makes the elevation
    // legible without relying on the light direction.
    auto emit = [&](int ax, int az, int bx, int bz, int cx, int cz) {
        const Vector3 a = corner(ax, az);
        const Vector3 b = corner(bx, bz);
        const Vector3 c = corner(cx, cz);

        // The face normal, from the cross product of two edges. The vertex
        // ORDER below is chosen so this comes out pointing upwards (+Y): the
        // graphics layer discards back faces, so a triangle wound the wrong way
        // round would simply be invisible from above.
        const Vector3 nrm = Vector3Normalize(
            Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));

        const Vector3 pts[3] = {a, b, c};
        const int     gx[3]  = {ax, bx, cx};
        const int     gz[3]  = {az, bz, cz};
        for (int k = 0; k < 3; ++k) {
            mesh.vertices[vi++] = pts[k].x;
            mesh.vertices[vi++] = pts[k].y;
            mesh.vertices[vi++] = pts[k].z;
            mesh.normals[ni++]  = nrm.x;
            mesh.normals[ni++]  = nrm.y;
            mesh.normals[ni++]  = nrm.z;
            mesh.texcoords[ti++] = (float)gx[k] / (float)cells;
            mesh.texcoords[ti++] = (float)gz[k] / (float)cells;
        }
    };

    for (int z = 0; z < cells; ++z) {
        for (int x = 0; x < cells; ++x) {
            // The two halves of this square, split along the (x,z)-(x+1,z+1)
            // diagonal so the surface matches the collision heightfield. Both
            // are wound to face upwards.
            emit(x, z,  x,     z + 1,  x + 1, z + 1);
            emit(x, z,  x + 1, z + 1,  x + 1, z    );
        }
    }

    // Hand the arrays to the graphics card. Without this the mesh exists in
    // main memory only and draws nothing.
    UploadMesh(&mesh, false);
    return mesh;
}

void TerrainComponent::EnsureBuilt() {
    if (m_tried) return;
    m_tried = true;

    // The mesh is built from SampleHeights - the very same function the
    // collision surface is built from. Sharing it is what guarantees the two
    // cannot drift apart: there is one definition of "how high is the ground
    // here", not two that merely happen to agree today.
    const int res = (resolution > 1) ? resolution : 2;
    const std::vector<float> heights = SampleHeights(res);
    if (heights.empty()) return;

    Mesh mesh = BuildHeightfieldMesh(heights, res, worldSize, maxHeight);
    m_model = LoadModelFromMesh(mesh);       // wrap the mesh in a drawable model
    ApplyLightingShader(m_model);            // shade the hills instead of flat green
    m_built = true;
}

std::vector<float> TerrainComponent::SampleHeights(int n) const {
    std::vector<float> out;
    if (n < 2) return out;
    out.resize((size_t)n * n, 0.0f);

    // Regenerate the very same noise image the mesh is built from. Identical
    // arguments give an identical image, which is what guarantees the
    // collision surface matches the hills that are drawn.
    const int res = (resolution > 1) ? resolution : 2;
    Image img = GenImagePerlinNoise(res, res, seed, seed, noiseScale);
    // LoadImageColors unpacks the image into a plain array of RGBA values,
    // whatever internal format it was stored in.
    Color* px = LoadImageColors(img);

    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            // Where this grid point falls in the source image. Both grids
            // cover the same square, so the mapping is a simple proportion:
            // grid index 0 is image pixel 0 and grid index n-1 is pixel res-1.
            const float fx = (float)x * (float)(res - 1) / (float)(n - 1);
            const float fz = (float)z * (float)(res - 1) / (float)(n - 1);

            // BILINEAR sampling: take the four surrounding pixels and blend
            // them by how close the sample lies to each. Picking the single
            // nearest pixel instead would produce a staircase of flat steps
            // that an aircraft would visibly catch on.
            const int x0 = (int)fx, z0 = (int)fz;
            const int x1 = (x0 + 1 < res) ? x0 + 1 : x0;
            const int z1 = (z0 + 1 < res) ? z0 + 1 : z0;
            const float tx = fx - (float)x0;
            const float tz = fz - (float)z0;

            // The noise image is grey, so any channel carries the height; red
            // is used, scaled from 0..255 down to 0..1.
            const float h00 = px[z0 * res + x0].r / 255.0f;
            const float h10 = px[z0 * res + x1].r / 255.0f;
            const float h01 = px[z1 * res + x0].r / 255.0f;
            const float h11 = px[z1 * res + x1].r / 255.0f;

            const float top    = h00 + (h10 - h00) * tx;   // blend along x
            const float bottom = h01 + (h11 - h01) * tx;
            out[(size_t)z * n + x] = top + (bottom - top) * tz;   // then along z
        }
    }

    UnloadImageColors(px);
    UnloadImage(img);
    return out;
}

void TerrainComponent::OnDraw(const Entity& owner) {
    EnsureBuilt();
    if (!m_built) return;
    // GenMeshHeightmap builds the terrain starting at a corner; offset by half
    // its width/depth so it's centered under this entity.
    Vector3 off = {-worldSize * 0.5f, 0.0f, -worldSize * 0.5f};
    DrawModel(m_model, off, 1.0f, tint);
    // Optional darker contour lines so the elevation is easy to read without
    // any lighting.
    if (wire) DrawModelWires(m_model, off, 1.0f, Color{0, 0, 0, 60});
}

void TerrainComponent::OnInspector() {
    // World size and height are dragged on a LOGARITHMIC scale and bounded only
    // by what the numbers themselves can express, not by a guess at what anyone
    // would want. A terrain may be a 100-metre test pad or a 100-kilometre
    // battlefield, and a linear drag across that span is useless at one end.
    //
    // The upper bounds are deliberately generous. A cap that is merely somebody's
    // idea of "big enough" does real harm here: DragFloat CLAMPS the value the
    // moment the widget is touched, so a terrain loaded from a scene file with a
    // larger number silently collapses to the cap as soon as anyone nudges the
    // slider - losing the setting with no warning and no undo.
    ImGui::DragFloat("World size", &worldSize, 5.0f, 20.0f, 200000.0f, "%.0f",
                     ImGuiSliderFlags_Logarithmic);
    ImGui::DragFloat("Max height", &maxHeight, 0.5f, 0.0f, 20000.0f, "%.0f",
                     ImGuiSliderFlags_Logarithmic);

    // Resolution is the ONE setting here with a real, physical ceiling, and it
    // is not about taste. The mesh is not indexed - every triangle carries its
    // own three vertices, each with a position, a normal and a texture
    // coordinate - so the memory cost grows with the SQUARE of this number:
    // resolution 1024 is roughly two million triangles and around 200 MB of
    // vertex data. A few thousand would exhaust memory outright, so this cap
    // stays, and the triangle readout below explains the cost on the way up.
    ImGui::DragInt("Resolution", &resolution, 1.0f, 8, 1024);

    // Point out when the visual and collision grids disagree, because nothing
    // else makes it visible: the landscape looks exactly right and only the
    // collisions are wrong, which is close to undiagnosable from the symptom.
    // A POWER OF TWO up to the collision grid's own ceiling makes the two match
    // sample for sample, which is why those values are worth preferring.
    const int physRes = TerrainCollisionGrid(resolution);
    if (physRes != resolution)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                           "collision grid only %d - use a power of two", physRes);

    // Hill scale is a FREQUENCY, not a size: it is how many noise periods span
    // the terrain, whatever the terrain measures. So the physical size of a hill
    // is worldSize / noiseScale, and widening the world without raising this
    // number stretches the same few hills over the larger area instead of adding
    // more. That is why the upper bound has to scale with ambition rather than
    // sit at a fixed number.
    ImGui::DragFloat("Hill scale", &noiseScale, 0.5f, 0.5f, 2000.0f, "%.1f",
                     ImGuiSliderFlags_Logarithmic);
    ImGui::DragInt("Seed", &seed);

    // ---- What those four numbers actually add up to -------------------------
    // The settings above are all relative to each other, so none of them means
    // anything alone. These derived figures are the ones worth tuning against:
    // how big a hill is on the ground, and how many grid cells describe it.
    if (resolution > 1 && noiseScale > 0.0f) {
        const float metresPerCell = worldSize / (float)(resolution - 1);
        const float metresPerHill = worldSize / noiseScale;
        const float cellsPerHill  = (float)resolution / noiseScale;

        ImGui::TextDisabled("%.0f m per hill, %.1f m per cell", metresPerHill, metresPerCell);

        // The sampling limit. A wave needs several samples per period to survive
        // being drawn on a grid; with fewer, neighbouring peaks fall between
        // cells and the landscape degenerates into noise that changes shape
        // completely for a one-step change in resolution. Around four cells per
        // hill is the point where it starts to show. This is a genuine limit of
        // sampling a signal on a grid, which is exactly why it belongs here as a
        // measured warning rather than as a fixed cap on the slider.
        if (cellsPerHill < 4.0f)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f),
                               "only %.1f cells per hill - raise Resolution to ~%d",
                               cellsPerHill, (int)(noiseScale * 4.0f));
        else if (cellsPerHill < 8.0f)
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                               "%.1f cells per hill - hills will look angular",
                               cellsPerHill);
    }

    // Show what the current resolution actually costs. Terrain is usually the
    // heaviest thing in a scene, and the cost is not obvious from the number:
    // resolution 500 is not "a bit more" than 250, it is four times as much.
    // Worse, the mesh is not indexed - every triangle carries its own three
    // vertices - so the vertex count is three times the triangle count.
    int tris = TriangleCount();
    if (tris >= 200000)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f),
                           "%d triangles - very heavy", tris);
    else if (tris >= 60000)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                           "%d triangles", tris);
    else
        ImGui::TextDisabled("%d triangles", tris);

    float col[4] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", col))
        tint = {(unsigned char)(col[0] * 255), (unsigned char)(col[1] * 255),
                (unsigned char)(col[2] * 255), (unsigned char)(col[3] * 255)};
    ImGui::Checkbox("Contour lines", &wire);

    // Regenerating the mesh is expensive, so it happens only when you ask,
    // not on every slider tweak.
    if (ImGui::Button("Regenerate")) Rebuild();
}

} // namespace eng
