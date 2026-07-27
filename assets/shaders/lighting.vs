#version 330

// Lighting vertex shader. Its only real job is to hand the fragment shader a
// world-space NORMAL - the little arrow that says which way a surface faces.
// Everything about how bright a surface looks comes from comparing that arrow
// with the direction the sunlight travels.
//
// The vertex inputs below are filled in by raylib; the names are fixed by the
// engine and must be spelled exactly like this to be connected automatically.

in vec3 vertexPosition;      // corner position, in the model's own space
in vec2 vertexTexCoord;      // where this corner samples the texture
in vec3 vertexNormal;        // which way the surface faces, in model space
in vec4 vertexColor;         // per-corner tint (raylib's primitives use this)

uniform mat4 mvp;            // model * view * projection, ready for the screen
uniform mat4 matNormal;      // turns a model-space normal into a world-space one

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main()
{
    // Pass the texture coordinate and vertex colour straight through.
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;

    // Normals need their own matrix, not the model matrix. Scaling an object
    // unevenly (say, twice as wide but the same height) would tilt a normal the
    // wrong way if it were transformed like a position; matNormal is the
    // inverse-transpose of the model matrix, which corrects for that.
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));

    // Position is transformed by the combined matrix raylib provides, exactly
    // as the engine's built-in shader does.
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
