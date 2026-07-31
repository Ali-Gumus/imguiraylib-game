#pragma once

#include "engine/Component.h"   // the Component base class
#include "raylib.h"

#include <memory>
#include <string>
#include <vector>

namespace eng {
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

    std::unique_ptr<Component> Clone() const override {
        auto c = std::make_unique<TerrainComponent>();
        c->worldSize  = worldSize;  c->maxHeight  = maxHeight;
        c->resolution = resolution; c->noiseScale = noiseScale;
        c->seed = seed; c->tint = tint; c->wire = wire;
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
        Rebuild();
    }

    // Settings you can tweak in the Inspector (press Regenerate to apply).
    float worldSize  = 400.0f;      // how many world units wide/deep the terrain is
    float maxHeight  = 25.0f;       // height of the tallest hills
    int   resolution = 80;          // grid detail (more = smoother, heavier)
    float noiseScale = 5.0f;        // hill frequency (higher = more, smaller hills)
    int   seed       = 0;           // change for a different random landscape
    Color tint       = DARKGREEN;
    bool  wire       = true;        // overlay contour lines so the hills read clearly

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
