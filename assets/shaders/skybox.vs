#version 330

// Skybox vertex shader. The mesh is a unit cube centered on the camera; each
// vertex's local position doubles as the view direction, which the fragment
// shader turns into a sky colour. Dropping the translation part of the view
// matrix keeps the sky centered on the camera no matter where it flies.

in vec3 vertexPosition;

uniform mat4 matView;        // set by raylib each draw
uniform mat4 matProjection;  // set by raylib each draw

// How big to draw the cube, in world units of radius. The cube surrounds the
// camera, so its faces sit this far away in every direction. That distance must
// land strictly BETWEEN the camera's near and far clip planes, or the sky is
// clipped away and the background turns to bare clear-colour.
//
// It has to be a uniform rather than a fixed size in the mesh because this
// shader never reads matModel: raylib's DrawModel scale argument arrives in
// that matrix, so it is silently ignored here and the mesh is always the size
// it was generated at. Scaling in the shader is what actually resizes the sky.
//
// The size is otherwise irrelevant - the cube is only a way to hand the
// fragment shader a direction, and depth writes are off while it draws, so it
// never occludes anything however large it is.
uniform float skyScale;

out vec3 fragPosition;       // the direction this cube corner points

void main()
{
    // The UNSCALED corner position is what the fragment shader wants: it is
    // used as a direction, and scaling a direction would not change the colour
    // but would make the uniform look like it mattered here. Keep them separate.
    fragPosition = vertexPosition;
    mat4 rotView = mat4(mat3(matView));            // keep rotation, drop translation
    gl_Position = matProjection * rotView * vec4(vertexPosition * skyScale, 1.0);
}
