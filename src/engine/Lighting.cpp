#include "engine/Lighting.h"

#include "raymath.h"    // Vector3Normalize

namespace eng {

// File-scope state. `static` here means "visible only inside this file", which
// keeps these names from colliding with anything else in the program.
static Shader      s_shader{};        // the compiled lighting shader
static bool        s_ready = false;   // did it compile?
static SunSettings s_sun{};           // the sun currently pushed to the shader

// Where each uniform lives inside the compiled shader program. Looking a
// uniform up by name costs a string comparison on the GPU driver's side, so we
// do it once at load and reuse the numbers every frame.
static int s_locSunDir    = -1;
static int s_locSunColor  = -1;
static int s_locAmbient   = -1;
static int s_locSky       = -1;
static int s_locGround    = -1;

bool InitLighting() {
    // Paths are relative to the working directory, which the program sets to
    // the project root at startup.
    s_shader = LoadShader("assets/shaders/lighting.vs", "assets/shaders/lighting.fs");

    // IsShaderValid tells us whether the files existed AND the GLSL compiled.
    // A driver that rejects the shader must not take the whole program down, so
    // failure here just means "render unlit", handled by every caller.
    s_ready = IsShaderValid(s_shader);
    if (!s_ready) return false;

    // GetShaderLocation returns -1 for a uniform the shader does not have (or
    // one the GLSL compiler removed because nothing used it). SetShaderValue
    // ignores a location of -1, so no extra checking is needed later.
    s_locSunDir   = GetShaderLocation(s_shader, "sunDirection");
    s_locSunColor = GetShaderLocation(s_shader, "sunColor");
    s_locAmbient  = GetShaderLocation(s_shader, "ambientColor");
    s_locSky      = GetShaderLocation(s_shader, "skyColor");
    s_locGround   = GetShaderLocation(s_shader, "groundColor");

    SetSun(s_sun);   // push the defaults so the shader is usable immediately
    return true;
}

void ShutdownLighting() {
    if (s_ready) {
        UnloadShader(s_shader);
        s_ready = false;
    }
}

bool   IsLightingReady()   { return s_ready; }
Shader GetLightingShader() { return s_shader; }

void ApplyLightingShader(Model& model) {
    if (!s_ready) return;
    for (int i = 0; i < model.materialCount; i++)
        model.materials[i].shader = s_shader;
}

void SetSun(const SunSettings& sun) {
    s_sun = sun;
    if (!s_ready) return;

    // The shader's angle maths assumes a unit-length direction, so normalize
    // whatever came in. A zero-length vector would divide by zero, so fall back
    // to straight down in that case.
    Vector3 dir = sun.direction;
    if (Vector3LengthSqr(dir) < 0.000001f) dir = {0.0f, -1.0f, 0.0f};
    dir = Vector3Normalize(dir);

    // SHADER_UNIFORM_VEC3 tells raylib the value is three floats.
    SetShaderValue(s_shader, s_locSunDir,   &dir,          SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locSunColor, &s_sun.color,  SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locAmbient,  &s_sun.ambient,SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locSky,      &s_sun.sky,    SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locGround,   &s_sun.ground, SHADER_UNIFORM_VEC3);
}

const SunSettings& GetSun() { return s_sun; }

} // namespace eng
