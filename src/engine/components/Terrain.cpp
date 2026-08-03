#include "engine/components/Terrain.h"

#include "engine/Scene.h"
#include "imgui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
// ---------------------------------------------------------------------------
// THE LANDSCAPE ITSELF
//
// The heights come from noise generated here rather than from raylib's
// GenImagePerlinNoise, and there are two reasons that matters.
//
// First, an image holds whole BYTES. A 900-metre landscape quantised to 256
// levels steps in 3.5-metre terraces, which are invisible on a hillside and
// glaring on a shallow slope - and an aircraft flying low catches on them.
// A function returns a real number and has no steps at all.
//
// Second, an image has to be RESAMPLED whenever something wants the ground at a
// different grid size than the picture, which the collision surface does. A
// continuous function is simply asked about the point in question, so the mesh
// and the collision surface agree by construction rather than by interpolating
// the same picture the same way and hoping.
// ---------------------------------------------------------------------------

// A repeatable pseudo-random number for an integer lattice point. Deterministic:
// the same x, z and seed always give the same answer, which is what lets the
// landscape be regenerated identically instead of stored.
static uint32_t LatticeHash(int x, int z, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u
               + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// The dot product of a corner's random GRADIENT with the offset to the sample
// point. Eight directions is plenty and avoids a table.
static float LatticeGrad(uint32_t hash, float x, float z) {
    switch (hash & 7u) {
        case 0:  return  x + z;
        case 1:  return  x - z;
        case 2:  return -x + z;
        case 3:  return -x - z;
        case 4:  return  x;
        case 5:  return -x;
        case 6:  return  z;
        default: return -z;
    }
}

// Perlin's fade curve. A plain linear blend between lattice corners leaves a
// visible crease along every cell edge, because the SLOPE jumps there even
// though the height does not. This curve is flat at both ends, so slopes match
// across a boundary and the surface is smooth rather than merely continuous.
static float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Gradient noise at a point, returning roughly -1..1.
static float GradientNoise(float x, float z, int seed) {
    const int   xi = (int)std::floor(x), zi = (int)std::floor(z);
    const float xf = x - (float)xi,      zf = z - (float)zi;
    const float u  = Fade(xf),           v  = Fade(zf);

    const float n00 = LatticeGrad(LatticeHash(xi,     zi,     seed), xf,        zf);
    const float n10 = LatticeGrad(LatticeHash(xi + 1, zi,     seed), xf - 1.0f, zf);
    const float n01 = LatticeGrad(LatticeHash(xi,     zi + 1, seed), xf,        zf - 1.0f);
    const float n11 = LatticeGrad(LatticeHash(xi + 1, zi + 1, seed), xf - 1.0f, zf - 1.0f);

    const float a = n00 + u * (n10 - n00);
    const float b = n01 + u * (n11 - n01);
    return a + v * (b - a);
}

// Sum several octaves of noise into a landscape, returning 0..1.
//
// Each octave doubles the frequency and halves the amplitude, which is the
// pattern natural terrain actually follows: a mountain range carries hills,
// hills carry spurs, spurs carry bumps, each smaller feature a fraction of the
// size of the one it sits on. Summing them is what makes a surface look eroded
// rather than drawn.
// How many of the requested octaves a mesh of this resolution can represent.
// Declared in the header so the Inspector can report the same number rather than
// working it out a second way and disagreeing.
int ResolvableOctaves(int octaves, int resolution, float noiseScale) {
    if (octaves < 1) return 1;
    const float cells = (float)((resolution > 1) ? resolution - 1 : 1);
    if (noiseScale <= 0.0f) return octaves;
    int usable = 1;
    for (int o = 1; o < octaves; ++o) {
        // The frequency this octave would add, in features across the map.
        const float freq = noiseScale * std::pow(2.0f, (float)o);
        if (cells / freq < 3.0f) break;    // fewer than three cells per feature
        usable = o + 1;
    }
    return usable;
}

static float TerrainFbm(float x, float z, int seed, int octaves, float ridge) {
    if (octaves < 1) octaves = 1;
    if (octaves > 10) octaves = 10;
    if (ridge < 0.0f) ridge = 0.0f;
    if (ridge > 1.0f) ridge = 1.0f;

    float sum = 0.0f, norm = 0.0f;
    float amp = 1.0f, freq = 1.0f;

    for (int o = 0; o < octaves; ++o) {
        // Each octave gets its own seed, or every layer would be the same
        // pattern at a different size and the sum would show that self-similar
        // grid rather than hiding it.
        const float n = GradientNoise(x * freq, z * freq, seed + o * 1013);

        // The ridged version: folding at zero turns each sign change into a
        // crease. Squaring sharpens the crease and rounds the ground between
        // creases, which is what makes ridge lines read as ridge lines.
        float folded = 1.0f - std::fabs(n);
        folded = folded * folded * 2.0f - 1.0f;

        sum  += (n + (folded - n) * ridge) * amp;
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.0f;
    }

    const float h = (norm > 0.0f) ? (sum / norm) : 0.0f;    // -1..1
    float unit = h * 0.5f + 0.5f;                            // 0..1

    // STRETCH, because a sum of random layers clusters around its middle. Each
    // octave is an independent wobble, and independent wobbles cancel more often
    // than they agree, so the total almost never reaches either extreme - a
    // five-octave sum measures about 0.13..0.80 rather than 0..1. Left alone
    // that is a map with no low ground and no summits, every square metre of it
    // halfway up a slope.
    unit = (unit - 0.5f) * 1.9f + 0.5f;
    if (unit < 0.0f) unit = 0.0f;
    if (unit > 1.0f) unit = 1.0f;

    // Then BIAS towards the low ground. Squaring pushes the middle down while
    // leaving the top alone - a half-height sample becomes quarter-height, but a
    // full-height one stays full - so the map becomes mostly low country with
    // peaks standing out of it, which is both what real terrain looks like and
    // what leaves an aircraft somewhere to fly that is not a mountainside.
    return unit * unit;
}

// ---------------------------------------------------------------------------
// SURFACE COLOUR
//
// A landscape is not one colour, and the two things that decide which colour a
// patch of ground is are how HIGH it is and how STEEP it is.
//
// Height gives the bands everyone recognises - shore, grass, scrub, bare rock,
// snow - because temperature and exposure change with altitude.
//
// Steepness then overrides all of it, and that is the part that makes terrain
// read as real rather than as a contour map: soil, grass and snow cannot cling
// to a cliff, so anything steep is bare rock whatever height it sits at. Without
// this rule the bands wrap around mountains like paint stripes.
// ---------------------------------------------------------------------------

static Color MixColor(Color a, Color b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return Color{(unsigned char)(a.r + (b.r - a.r) * t),
                 (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t),
                 255};
}

// Smooth 0..1 ramp between two thresholds, so bands blend instead of banding.
static float Ramp(float v, float lo, float hi) {
    if (hi <= lo) return 0.0f;
    float t = (v - lo) / (hi - lo);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// `h01` is height as a fraction of the tallest peak; `up` is the surface
// normal's vertical part, 1 flat and 0 vertical.
static Color TerrainSurfaceColor(float h01, float up, uint32_t jitter) {
    const Color kShore{194, 178, 128, 255};
    const Color kGrass{ 74, 110,  52, 255};
    const Color kMeadow{ 96, 124,  58, 255};
    const Color kScrub{112, 104,  64, 255};
    const Color kRock { 104,  98,  92, 255};
    const Color kSnow { 238, 240, 245, 255};

    Color c = kShore;
    c = MixColor(c, kGrass,  Ramp(h01, 0.010f, 0.045f));
    c = MixColor(c, kMeadow, Ramp(h01, 0.045f, 0.30f));
    c = MixColor(c, kScrub,  Ramp(h01, 0.30f,  0.52f));
    c = MixColor(c, kRock,   Ramp(h01, 0.52f,  0.72f));
    c = MixColor(c, kSnow,   Ramp(h01, 0.74f,  0.92f));

    // Anything steeper than about forty degrees is scoured back to rock. The
    // ramp runs the other way round - a LOWER `up` is steeper - so the arguments
    // are reversed rather than the result subtracted.
    c = MixColor(c, kRock, 1.0f - Ramp(up, 0.62f, 0.86f));

    // A few percent of brightness variation per face. Real ground is never one
    // even wash, and without this the blends between bands are so clean that
    // they read as gradients painted on rather than as ground.
    const float n = 0.94f + (float)(jitter & 0xFFu) / 255.0f * 0.12f;
    auto scale = [n](unsigned char v) {
        const float f = (float)v * n;
        return (unsigned char)(f > 255.0f ? 255.0f : f);
    };
    return Color{scale(c.r), scale(c.g), scale(c.b), 255};
}

static Mesh BuildHeightfieldMesh(const std::vector<float>& heights, int n,
                                 float worldSize, float maxHeight,
                                 bool naturalColor, bool smooth) {
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
    // Four bytes of colour per vertex. The lighting shader already forwards
    // vertexColor and multiplies it into the surface colour, so filling this in
    // is the whole of what it takes to paint the landscape - no texture, no
    // extra uniform, no shader change.
    mesh.colors    = (unsigned char*)RL_MALLOC((size_t)mesh.vertexCount * 4);

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

    // The surface normal AT A SAMPLE, worked out from how fast the ground rises
    // either side of it rather than from any one triangle.
    //
    // This is what makes the landscape shade smoothly. A face normal is constant
    // across its triangle, so every triangle takes a single brightness and the
    // hillside becomes a mosaic. Sampling the slope at the CORNERS instead lets
    // the shading vary continuously across each face, and neighbouring faces
    // agree along their shared edge because they ask the same question about the
    // same point.
    //
    // The two spans are 2*step apart - one sample either side - and the edges
    // clamp to the last sample, which just means the outermost row is shaded as
    // though the ground continued flat.
    auto sampleNormal = [&](int x, int z) -> Vector3 {
        const int xm = (x > 0) ? x - 1 : x, xp = (x < n - 1) ? x + 1 : x;
        const int zm = (z > 0) ? z - 1 : z, zp = (z < n - 1) ? z + 1 : z;
        const float hx = (heights[(size_t)z * n + xp] - heights[(size_t)z * n + xm]) * maxHeight;
        const float hz = (heights[(size_t)zp * n + x] - heights[(size_t)zm * n + x]) * maxHeight;
        const float dx = (float)(xp - xm) * step;
        const float dz = (float)(zp - zm) * step;
        // The cross product of the two tangents, written out: a surface rising
        // to the east leans its normal to the west, hence the negated slopes.
        return Vector3Normalize(Vector3{-hx * dz, dx * dz, -hz * dx});
    };

    int vi = 0, ni = 0, ti = 0, ci = 0;

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
            // Smooth shading takes each corner's own slope; flat shading gives
            // all three corners the face's single normal.
            const Vector3 vn = smooth ? sampleNormal(gx[k], gz[k]) : nrm;

            mesh.vertices[vi++] = pts[k].x;
            mesh.vertices[vi++] = pts[k].y;
            mesh.vertices[vi++] = pts[k].z;
            mesh.normals[ni++]  = vn.x;
            mesh.normals[ni++]  = vn.y;
            mesh.normals[ni++]  = vn.z;
            mesh.texcoords[ti++] = (float)gx[k] / (float)cells;
            mesh.texcoords[ti++] = (float)gz[k] / (float)cells;

            Color col{255, 255, 255, 255};
            if (naturalColor) {
                // Coloured per CORNER, so the bands blend across a face instead
                // of changing at its edges. The jitter is keyed to the grid
                // position rather than to the vertex, or the three corners of a
                // face would each get their own speckle and the surface would
                // fizz.
                const float h01 = heights[(size_t)gz[k] * n + gx[k]];
                col = TerrainSurfaceColor(h01, vn.y,
                                          LatticeHash(gx[k] / 3, gz[k] / 3, 7));
            }
            mesh.colors[ci++] = col.r;
            mesh.colors[ci++] = col.g;
            mesh.colors[ci++] = col.b;
            mesh.colors[ci++] = col.a;
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

    Mesh mesh = BuildHeightfieldMesh(heights, res, worldSize, maxHeight,
                                     naturalColor, smooth);
    m_model = LoadModelFromMesh(mesh);       // wrap the mesh in a drawable model
    ApplyLightingShader(m_model);            // shade the hills instead of flat green
    m_built = true;
}

std::vector<float> TerrainComponent::SampleHeights(int n) const {
    std::vector<float> out;
    if (n < 2) return out;
    out.resize((size_t)n * n, 0.0f);

    // The landscape is a FUNCTION of position, so any grid size samples the same
    // surface. That is what guarantees the collision surface matches the hills
    // that are drawn even though physics asks for a coarser grid than the mesh:
    // there is one definition of "how high is the ground here", evaluated twice,
    // rather than one picture interpolated two ways.
    //
    // Positions are normalised to 0..1 across the terrain before being scaled by
    // noiseScale, so the landscape keeps its shape when the resolution changes
    // and `noiseScale` goes on meaning what it always meant - how many hills fit
    // across the map.
    // How many octaves this landscape can actually SHOW. Each octave doubles the
    // frequency, so past a point the detail is finer than the distance between
    // mesh samples and cannot be drawn at all: it does not add roughness, it
    // adds noise, and it makes the ground disagree with itself between one
    // sample and the next. Three cells per feature is the cutoff.
    //
    // CRITICALLY, this is computed from `resolution` and NOT from `n`. The
    // collision surface asks for a coarser grid than the mesh does, and if the
    // limit followed the grid being asked for, the two would evaluate DIFFERENT
    // functions - the collision surface would be built from a genuinely
    // different landscape, not merely a coarser sampling of the same one. That
    // is the same class of bug as the mismatched triangle diagonals, and just as
    // invisible to looking at it.
    const int useOctaves = ResolvableOctaves(octaves, resolution, noiseScale);

    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const float u = (float)x / (float)(n - 1);
            const float v = (float)z / (float)(n - 1);
            out[(size_t)z * n + x] =
                TerrainFbm(u * noiseScale, v * noiseScale, seed, useOctaves, ridge);
        }
    }

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

    // How the landscape is SHAPED, as opposed to how big it is.
    ImGui::SliderInt("Detail layers", &octaves, 1, 8);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Layers of noise summed together. 1 is smooth rolling\n"
                          "blobs; each layer adds finer detail at half the height.");
    // Say so when layers are being ignored. Silently clamping would leave the
    // slider claiming detail that is not in the landscape, and the natural
    // response - dragging it further right - would do nothing at all.
    {
        const int usable = ResolvableOctaves(octaves, resolution, noiseScale);
        if (usable < octaves)
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                               "  using %d of %d - finer detail needs more resolution",
                               usable, octaves);
    }
    ImGui::SliderFloat("Ridges", &ridge, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("0 gives rounded hills, 1 gives creased mountain\n"
                          "ridges and valley floors.");

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
    ImGui::Checkbox("Natural colour", &naturalColor);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Colour the ground by height and steepness - shore,\n"
                          "grass, scrub, rock and snow, with cliffs bare.\n"
                          "Tint multiplies this, so keep Tint white.");
    ImGui::SameLine();
    ImGui::Checkbox("Smooth", &smooth);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shade as a continuous surface rather than as\n"
                          "individual triangles.");

    ImGui::Checkbox("Contour lines", &wire);

    // Regenerating the mesh is expensive, so it happens only when you ask,
    // not on every slider tweak.
    if (ImGui::Button("Regenerate")) Rebuild();
}

} // namespace eng
