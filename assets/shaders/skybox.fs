#version 330

// Procedural gradient sky. No texture: the colour comes purely from the view
// direction's height (fragPosition.y after normalising). Up is deep blue, the
// horizon is pale, and below the horizon fades to a dull ground tone.

in vec3 fragPosition;
out vec4 finalColor;

void main()
{
    vec3  dir = normalize(fragPosition);
    float t   = dir.y;                       // -1 straight down .. +1 straight up

    vec3 zenith  = vec3(0.20, 0.42, 0.82);   // sky overhead
    vec3 horizon = vec3(0.78, 0.86, 0.96);   // pale band at eye level
    vec3 ground  = vec3(0.32, 0.30, 0.27);   // muted tone below the horizon

    vec3 col;
    if (t > 0.0) col = mix(horizon, zenith, pow(clamp( t, 0.0, 1.0), 0.55));
    else         col = mix(horizon, ground, pow(clamp(-t, 0.0, 1.0), 0.45));

    finalColor = vec4(col, 1.0);
}
