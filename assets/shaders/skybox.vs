#version 330

// Skybox vertex shader. The mesh is a unit cube centered on the camera; each
// vertex's local position doubles as the view direction, which the fragment
// shader turns into a sky colour. Dropping the translation part of the view
// matrix keeps the sky centered on the camera no matter where it flies.

in vec3 vertexPosition;

uniform mat4 matView;        // set by raylib each draw
uniform mat4 matProjection;  // set by raylib each draw

out vec3 fragPosition;       // the direction this cube corner points

void main()
{
    fragPosition = vertexPosition;
    mat4 rotView = mat4(mat3(matView));            // keep rotation, drop translation
    gl_Position = matProjection * rotView * vec4(vertexPosition, 1.0);
}
