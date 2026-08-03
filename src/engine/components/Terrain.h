#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {

// How many of `octaves` a terrain mesh of this resolution can actually show.
// Detail finer than about three mesh cells cannot be drawn and only adds noise,
// so the generator silently stops there; the Inspector reports the number so the
// limit is visible rather than mysterious.
int ResolvableOctaves(int octaves, int resolution, float noiseScale);

// Procedurally generates and draws a 3D terrain mesh with rolling hills. The
// heights come from Perlin noise (a smooth, natural-looking random pattern),
// so you get real elevation to fly over instead of a flat plane. The mesh is
// built once (lazily) and rebuilt when you change the settings.
class TerrainComponent : public Component {
public:
    TerrainComponent() = default;
    // Owns a GPU mesh, so (like ModelComponent) it must not be copied.
    TerrainComponent(const TerrainComponent&) = delete;
    TerrainComponent& operator=(const TerrainComponent&) = delete;
    ~TerrainComponent() override;

    const char* Name() const override { return "Terrain"; }

    // Every field that Serialize writes must be copied here too, or Play/Stop
    // silently reverts it: the snapshot taken on Play is what Stop restores, so
    // anything a Clone drops is erased from the project rather than merely
    // missing from a copy.
    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<TerrainComponent>();
        c->worldSize  = worldSize;  c->maxHeight  = maxHeight;
        c->resolution = resolution; c->noiseScale = noiseScale;
        c->seed = seed; c->tint = tint; c->wire = wire;
        c->octaves = octaves; c->ridge = ridge;
        c->naturalColor = naturalColor; c->smooth = smooth;
        return c;
    }

    void OnDraw(const Entity& owner) override;
    void OnInspector() override;

    void Serialize(nlohmann::json& out) const override {
        out["worldSize"]  = worldSize;  out["maxHeight"]  = maxHeight;
        out["resolution"] = resolution; out["noiseScale"] = noiseScale;
        out["seed"] = seed;
        out["tint"] = {tint.r, tint.g, tint.b, tint.a};
        out["wire"] = wire;
        out["octaves"] = octaves; out["ridge"] = ridge;
        out["naturalColor"] = naturalColor; out["smooth"] = smooth;
    }
    void Deserialize(const nlohmann::json& in) override {
        worldSize  = in.value("worldSize", worldSize);
        maxHeight  = in.value("maxHeight", maxHeight);
        resolution = in.value("resolution", resolution);
        noiseScale = in.value("noiseScale", noiseScale);
        seed       = in.value("seed", seed);
        if (in.contains("tint"))
            tint = {in["tint"][0], in["tint"][1], in["tint"][2], in["tint"][3]};
        wire = in.value("wire", wire);
        octaves      = in.value("octaves", octaves);
        ridge        = in.value("ridge", ridge);
        naturalColor = in.value("naturalColor", naturalColor);
        smooth       = in.value("smooth", smooth);
        Rebuild();
    }

    // Settings you can tweak in the Inspector (press Regenerate to apply).
    float worldSize  = 400.0f;      // how many world units wide/deep the terrain is
    float maxHeight  = 25.0f;       // height of the tallest hills
    int   resolution = 80;          // grid detail (more = smoother, heavier)
    float noiseScale = 5.0f;        // hill frequency (higher = more, smaller hills)
    int   seed       = 0;           // change for a different random landscape

    // A MULTIPLIER over the surface colour, not the colour itself. White leaves
    // the landscape its own colouring; anything else washes the whole terrain
    // towards that hue, which is how you get an alien world or a dusk pass.
    //
    // It used to be the only colour the terrain had, so a scene authored before
    // naturalColor existed carries a solid green here and will look like the old
    // flat terrain until it is set to white.
    Color tint       = WHITE;
    bool  wire       = false;       // overlay contour lines so the hills read clearly

    // How many layers of noise are summed to make the landscape. ONE octave is a
    // single smooth wave - rolling blobs of the same size everywhere, which is
    // what a heightmap looks like before anyone adds detail to it. Each further
    // octave adds a wave at twice the frequency and half the height, so hills
    // acquire spurs, spurs acquire bumps, and the surface stops looking like it
    // was made by one process. Five is enough that the eye stops finding the
    // pattern; past about eight the extra layers are finer than the mesh can
    // show and only cost time.
    int   octaves = 5;

    // How mountainous the landscape is, from 0 to 1.
    //
    // Plain noise makes rounded hills, because it varies smoothly through zero.
    // Folding it at zero instead - taking one minus its absolute value - turns
    // every crossing into a CREASE, and creases read as ridge lines and valley
    // floors. That is the difference between countryside and a mountain range,
    // and it is worth knowing that real mountains are ridged for exactly this
    // reason: they are erosion creases, not heaps.
    float ridge = 0.45f;

    // Colour the surface by height and steepness - shore, grass, scrub, rock and
    // snow, with anything too steep to hold them showing bare rock. Off gives
    // the older single-colour terrain, which is flatter looking but cheaper to
    // reason about when debugging shape rather than appearance.
    bool  naturalColor = true;

    // Average the normals across neighbouring faces so the surface shades as a
    // continuous landscape rather than as a field of triangles. Off restores the
    // faceted look, which pairs with `wire` to make elevation legible while
    // authoring - useful for judging shape, wrong for looking real.
    bool  smooth = true;

    void Rebuild();                 // discard the mesh so it regenerates next draw

    // The terrain's height at each point of an n x n grid, as a value from 0
    // (lowest) to 1 (the full maxHeight), laid out row by row so that the
    // sample for grid position (x, z) is at index z * n + x.
    //
    // This exists so the physics engine can build a collision surface from the
    // same landscape that is drawn, without needing to know anything about
    // Perlin noise or raylib images. It re-derives the heights from the
    // settings rather than reading the built mesh, so it works even before the
    // terrain has ever been drawn - which matters because collision bodies are
    // created when play starts, and the Game view may not have drawn yet.
    //
    // `n` need not match `resolution`: the grid is sampled smoothly, so the
    // collision surface can be coarser than the visible mesh. It is a separate
    // number because the physics engine constrains what sizes it will accept.
    std::vector<float> SampleHeights(int n) const;

    // How many triangles the terrain mesh is made of. The heightmap is turned
    // into a grid of quads - one per group of four neighbouring pixels - and
    // each quad is two triangles, so the count grows with the SQUARE of the
    // resolution: doubling it makes four times the geometry. This is computed
    // from the settings, so it answers "what would this cost?" even before the
    // mesh has been built.
    int TriangleCount() const {
        int cells = (resolution > 1) ? (resolution - 1) : 0;
        return cells * cells * 2;
    }

private:
    void EnsureBuilt();
    Model m_model{};
    bool  m_built = false;
    bool  m_tried = false;
};
} // namespace eng
