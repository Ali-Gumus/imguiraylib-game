#pragma once

// ============================================================================
// Lighting: the engine's single directional light ("the sun") and the shader
// that applies it.
// ----------------------------------------------------------------------------
// Without a lighting shader, raylib draws every surface at its flat base
// colour: a cube looks like a coloured silhouette because all six faces come
// out identical, and a hill is indistinguishable from a flat field. Shading
// each surface by the angle it makes with a light source is what makes 3D
// objects read as solid.
//
// One shader is shared by everything in the world, and it is applied in two
// different ways depending on how a thing is drawn:
//   * Loaded MODELS and the terrain mesh carry a material; the shader is
//     assigned to that material once, when the model loads.
//   * The simple PRIMITIVES (cubes, spheres) are drawn straight to raylib's
//     batch, which uses whichever shader is active; Scene::Draw switches this
//     shader on around them.
//
// All of it is optional: if the shader files are missing or fail to compile,
// IsLightingReady() stays false and the engine draws exactly as it did before,
// unlit but working.
// ============================================================================

#include "raylib.h"     // Shader, Vector3, Color

namespace eng {

// Load the lighting shader from assets/shaders/. Call this ONCE after the
// window exists (a shader is a GPU object, and there is no GPU to talk to
// before the window is created). Returns false if the shader did not compile,
// in which case the engine simply renders unlit.
bool InitLighting();

// Free the shader. Call once at shutdown, before the window closes.
void ShutdownLighting();

// Did the shader load? Everything else here is harmless to call either way.
bool IsLightingReady();

// The shader itself, so a material can be pointed at it.
Shader GetLightingShader();

// Point every material a model owns at the lighting shader, so the model is
// shaded instead of drawn at a flat, uniform colour. Call it once, right after
// loading or generating the model. A model can be split into several materials
// (a jet might have one for the body and one for the canopy) and each carries
// its own shader, so all of them are set. Does nothing when the shader failed
// to load, which leaves raylib's default in place.
void ApplyLightingShader(Model& model);

// Describes the one directional light in the scene. A directional light has no
// position - only a direction - because it stands for something so far away
// (the sun) that its rays arrive parallel everywhere in the world.
struct SunSettings {
    // The direction the light TRAVELS, as a vector. The default points down
    // and to one side, like mid-afternoon sun: light going down (-Y), forward
    // (-Z) and slightly right (+X) strikes upward and side faces differently,
    // which is what reveals an object's shape.
    Vector3 direction{0.35f, -0.85f, -0.4f};

    // Colour and strength of the sunlight. Values above 1 make it brighter
    // than the surface's own colour; a warm tint (more red than blue) reads as
    // daylight.
    Vector3 color{1.05f, 1.0f, 0.9f};

    // Light that reaches surfaces the sun cannot see. Without it, every
    // shadowed face would be pure black.
    Vector3 ambient{0.25f, 0.26f, 0.3f};

    // A faint tint added to upward-facing surfaces (as if from the sky) and to
    // downward-facing ones (as if bounced off the ground). Small values: this
    // is a subtle effect that keeps flat shading from looking like paper.
    Vector3 sky{0.05f, 0.07f, 0.12f};
    Vector3 ground{0.08f, 0.06f, 0.04f};
};

// Push a sun description to the shader. Call once per frame, before drawing.
// The direction is normalized here, so any non-zero vector works.
void SetSun(const SunSettings& sun);

// The sun currently in use, so the editor can show and edit it.
const SunSettings& GetSun();

} // namespace eng
