#version 330

// Lighting fragment shader: decides the final colour of every pixel of every
// surface in the world.
//
// The model used here is "Lambert diffuse plus ambient", the simplest lighting
// that still reads as three-dimensional:
//
//   * A DIRECTIONAL light (the sun) has no position, only a direction. Every
//     surface in the world receives it from the same angle, which is a good
//     description of something as far away as the sun.
//   * How brightly a surface catches that light depends only on the ANGLE
//     between the surface and the light. A face turned straight into the sun
//     gets the full amount; one turned edge-on gets almost nothing; one facing
//     away gets none. The dot product of two unit vectors gives exactly that
//     falloff, which is Lambert's cosine law.
//   * AMBIENT is a flat amount added everywhere, standing in for the light that
//     bounces off the ground and sky in the real world. Without it, surfaces
//     facing away from the sun would be pure black and unreadable.
//   * SKY FILL tints the ambient by whether a surface looks up or down, so
//     upward faces pick up a little sky colour and downward faces a little
//     ground colour. It is a cheap trick that stops flat shading from looking
//     like cardboard.

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform sampler2D texture0;   // the surface's texture (white if it has none)
uniform vec4 colDiffuse;      // the tint raylib passes per draw call

uniform vec3 sunDirection;    // the direction the light TRAVELS, normalized
uniform vec3 sunColor;        // sunlight colour and strength (1,1,1 = full white)
uniform vec3 ambientColor;    // light present even in shadow
uniform vec3 skyColor;        // tint added to upward-facing surfaces
uniform vec3 groundColor;     // tint added to downward-facing surfaces

out vec4 finalColor;

void main()
{
    // The surface's own colour: its texture, times the draw call's tint, times
    // the per-vertex colour. This is what the unlit engine shader produces.
    vec4 albedo = texture(texture0, fragTexCoord) * colDiffuse * fragColor;

    // Interpolating normals across a triangle can shorten them, so make the
    // vector unit-length again before using it in angle maths.
    vec3 n = normalize(fragNormal);

    // The light travels along sunDirection, so the direction TOWARDS the light
    // is the opposite of it. dot(n, toLight) is 1 when the surface faces the
    // light head-on and 0 or less when it faces away; max() clamps the "away"
    // case to zero instead of letting it darken the surface below black.
    vec3  toLight = normalize(-sunDirection);
    float diffuse = max(dot(n, toLight), 0.0);

    // Blend between the ground tint and the sky tint by how far the surface
    // tilts upward. n.y runs -1 (facing straight down) to +1 (straight up), so
    // the *0.5 + 0.5 rescales it to the 0..1 that mix() expects.
    vec3 fill = mix(groundColor, skyColor, n.y * 0.5 + 0.5);

    // Total incoming light: the sun where it lands, plus the ambient everywhere.
    vec3 lit = albedo.rgb * (sunColor * diffuse + ambientColor + fill);

    // Keep the original alpha so transparent things stay transparent.
    finalColor = vec4(lit, albedo.a);
}
