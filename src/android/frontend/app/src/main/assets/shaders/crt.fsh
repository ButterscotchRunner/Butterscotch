#version 300 es

// A simple CRT shader that *evokes* the feeling of a CRT, because real CRT shaders don't look that good on a phone
precision highp float;

in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform vec2 uResolution;
out vec4 fragColor;

uniform float uCurvature;
uniform float uAberration;
uniform float uHalation;
uniform float uScanlines;
uniform float uMask;
uniform float uVignette;

// Gentle barrel distortion so the image bows out like the curved glass of a CRT
vec2 curveRemap(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    vec2 offset = abs(uv.yx) / vec2(7.0, 5.0);
    uv += uv * offset * offset * uCurvature;
    return uv * 0.5 + 0.5;
}

// Sample the source texture and decode to linear light, used by the halation glow
vec3 sampleLinear(vec2 sampleUv) {
    return pow(texture(uTexture, sampleUv).rgb, vec3(2.2));
}

void main() {
    vec2 uv = curveRemap(vTexCoord);

    // Hard cutoff only well past the panel edge. The small margin keeps the feathered bezel band (further down) shaded instead of clipped
    if (uv.x < -0.01 || uv.x > 1.01 || uv.y < -0.01 || uv.y > 1.01) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Subtle horizontal chromatic aberration, stronger toward the edges where a CRT lens fringes the most
    float edge = distance(uv, vec2(0.5));
    float aberration = edge / uResolution.x * 4.0 * uAberration;
    vec3 color;
    color.r = texture(uTexture, uv + vec2(aberration, 0.0)).r;
    color.g = texture(uTexture, uv).g;
    color.b = texture(uTexture, uv - vec2(aberration, 0.0)).b;

    // The sampled texels are sRGB encoded, but the beam/scanline/mask math models actual light intensity, so do it in linear space
    // Multiplying scanlines straight onto sRGB values crushes the midtones and reads muddy, working in linear keeps it looking lit from behind
    color = pow(color, vec3(2.2));

    // Halation: bright phosphors bleed warm light into their neighbours. Sample a ring around the texel, keep only the bright part and add it back
    // This is the most expensive part (8 extra texture taps), drop to the 4 diagonals if it is too heavy on weaker devices
    vec2 glowRadius = vec2(2.5) / uResolution;
    vec3 glow = vec3(0.0);
    glow += sampleLinear(uv + glowRadius * vec2( 1.0,  0.0));
    glow += sampleLinear(uv + glowRadius * vec2(-1.0,  0.0));
    glow += sampleLinear(uv + glowRadius * vec2( 0.0,  1.0));
    glow += sampleLinear(uv + glowRadius * vec2( 0.0, -1.0));
    glow += sampleLinear(uv + glowRadius * vec2( 1.0,  1.0));
    glow += sampleLinear(uv + glowRadius * vec2(-1.0,  1.0));
    glow += sampleLinear(uv + glowRadius * vec2( 1.0, -1.0));
    glow += sampleLinear(uv + glowRadius * vec2(-1.0, -1.0));
    glow = max(glow * 0.125 - 0.5, 0.0) * vec3(1.0, 0.85, 0.7);
    color += glow * 0.6 * uHalation;

    // Linear luminance, used to bloom the scanlines: a real beam fattens in bright areas so the lines nearly vanish in highlights
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Scanlines derived from the CURVED uv so they bow with the glass, with the count tied to resolution so density is consistent across devices
    // fract keeps the sin argument tiny, dodging mobile GPU large argument precision loss on tall screens
    // A sharpened (pow) dark band keeps the lines visible on dense phone panels instead of averaging out
    float lines = uResolution.y / 5.0;
    float scanPhase = fract(uv.y * lines);
    float scan = pow(0.5 + 0.5 * sin(scanPhase * 6.2831853), 2.0);
    float scanDepth = mix(0.35, 0.08, lum) * uScanlines;
    color *= 1.0 - scanDepth * scan;

    // RGB phosphor triads: tint each 3 physical pixel column toward R, G or B for a hint of real aperture grille structure
    // Kept subtle on purpose, at native phone resolution a strong mask moires and colour fringes
    int triad = int(mod(gl_FragCoord.x, 3.0));
    vec3 maskTint = triad == 0 ? vec3(1.0, 0.6, 0.6) : (triad == 1 ? vec3(0.6, 1.0, 0.6) : vec3(0.6, 0.6, 1.0));
    color *= mix(vec3(1.0), maskTint, 0.25 * uMask);

    // Vignette so the brightness falls off toward the corners
    float vignette = uv.x * (1.0 - uv.x) * uv.y * (1.0 - uv.y);
    vignette = clamp(pow(vignette * 16.0, 0.20), 0.0, 1.0);
    color *= mix(1.0, vignette, uVignette);

    // Feather the bezel edge so the curvature cutoff is anti-aliased into a soft border instead of a hard jagged line
    vec2 feather = 2.0 / uResolution;
    vec2 borderXY = smoothstep(vec2(0.0), feather, uv) * smoothstep(vec2(0.0), feather, 1.0 - uv);
    color *= borderXY.x * borderXY.y;

    // Compensate for the darkening the scanlines, mask and vignette introduce
    color *= 1.18;

    // Back to sRGB for display
    color = pow(color, vec3(1.0 / 2.2));

    fragColor = vec4(color, 1.0);
}
