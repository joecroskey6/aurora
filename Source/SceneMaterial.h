#pragma once

// Original material shared by the five geometric Aurora scenes. Append after
// PearlFlowMaterial.h, which declares flowReady/flowDye/flowVelocity/flowTexel.
// No geometry, clock, palette, texture or framebuffer state is owned here.
inline const char* sceneMaterialFunctions = R"GLSL(
const float auroraSurfaceTau = 6.28318530718;

vec3 auroraSurfaceNormal(vec3 value, vec3 fallback) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 0.0000000001 && lengthSquared < 100000000.0
        ? value * inversesqrt(lengthSquared) : fallback;
}

vec3 auroraSurfacePalette(float position, vec3 a, vec3 b, vec3 c) {
    float t = clamp(position, 0.0, 1.0);
    return t < 0.5 ? mix(a, b, smoothstep(0.0, 0.5, t))
                   : mix(b, c, smoothstep(0.5, 1.0, t));
}

// Coordinates are cycles. Fade unresolved detail before it crosses Nyquist;
// anti-aliasing the line edge alone would still leave moving moire patterns.
float auroraSurfaceLine(float coordinate, float halfWidth) {
    float pixelWidth = max(fwidth(coordinate), 0.0001);
    float distanceToLine = abs(fract(coordinate + 0.5) - 0.5);
    float coverage = 1.0 - smoothstep(halfWidth - pixelWidth,
                                     halfWidth + pixelWidth, distanceToLine);
    return coverage * (1.0 - smoothstep(0.28, 0.75, pixelWidth));
}

float auroraSurfaceWave(float coordinate) {
    float visibility = 1.0 - smoothstep(0.22, 0.65, fwidth(coordinate));
    return 0.5 + 0.5 * cos(auroraSurfaceTau * coordinate) * visibility;
}

vec3 auroraSurfacePigment(vec2 coordinate, float phase) {
    if (flowReady == 1)
        return max(texture2D(flowDye, clamp(coordinate, vec2(0.002), vec2(0.998))).rgb,
                   vec3(0.0));
    // Deterministic fallback: broad colour strata, still driven by supplied phase.
    float field = coordinate.x * 5.0 + coordinate.y * 2.0
                + 0.34 * sin(coordinate.y * 9.0 + phase * 0.12);
    return 0.06 + 0.12 * (0.5 + 0.5 * cos(field + vec3(0.0, 2.0944, 4.1888)));
}

vec3 auroraSurfacePigmentColour(vec3 pigment, vec3 a, vec3 b, vec3 c) {
    vec3 weight = pow(pigment + vec3(0.0001), vec3(2.6));
    return (a * weight.x + b * weight.y + c * weight.z)
         / max(dot(weight, vec3(1.0)), 0.0000001);
}

vec3 shadeAuroraSurface(vec3 world, vec2 uv, float frequency,
                       vec3 normal, vec3 view, vec3 a, vec3 b, vec3 c,
                       vec3 energy, float onset, float phase, int mode, int theme) {
    vec3 drive = clamp(energy, vec3(0.0), vec3(2.0));
    float pulse = clamp(onset, 0.0, 1.0);
    float spectral = clamp(frequency, 0.0, 4.0);
    vec3 n = auroraSurfaceNormal(normal, vec3(0.0, 0.0, 1.0));
    vec3 eye = auroraSurfaceNormal(view, vec3(0.0, 0.0, 1.0));
    float u = uv.x * auroraSurfaceTau, v = uv.y * auroraSurfaceTau;
    bool closed = mode == 1 || mode == 5;

    // Closed surfaces sample a periodic path, with no jump at either UV seam.
    // Open surfaces crop into the pigment's active central region.
    vec2 fieldUV = closed
        ? vec2(0.5 + 0.34 * cos(u) + 0.055 * cos(v),
               0.5 + 0.115 * sin(u) + 0.075 * sin(v))
        : vec2(0.08 + uv.x * 0.84, 0.29 + uv.y * 0.42);
    if (mode == 3)
        fieldUV = vec2(0.12 + uv.x * 0.76, 0.35 + uv.y * 0.30);

    vec2 current = vec2(0.0);
    if (flowReady == 1)
        current = clamp(texture2D(flowVelocity, fieldUV).xy * 0.00038,
                        vec2(-0.025), vec2(0.025));
    vec3 pigment = auroraSurfacePigment(fieldUV + current, phase);
    vec3 lowerPigment = auroraSurfacePigment(fieldUV - current * 1.7
                                           + n.xy * 0.026, phase);
    float density = dot(pigment, vec3(0.38, 0.42, 0.20));
    float lowerDensity = dot(lowerPigment, vec3(0.38, 0.42, 0.20));
    float pigmentCoverage = smoothstep(0.015, 0.24, dot(pigment, vec3(1.0)));

    float contour = world.x * 0.54 + world.y * 1.05 + density * 3.3;
    float fineCoordinate = uv.y * 112.0 + density * 7.0;
    float crossCoordinate = uv.x * 58.0 - lowerDensity * 5.0;
    float relief = 0.0, glazing = 0.62, lightTrace = 0.0;
    float palettePosition = 0.5 + 0.32 * sin(world.x * 1.1 + world.y * 0.7);

    if (mode == 0) {
        // Folded Light: long flowing laminae, engraved silk and subsurface folds.
        contour = uv.y * 5.0 + 0.38 * sin(u * 1.5 + phase * 0.07)
                + density * (3.8 + 0.5 * drive.y);
        fineCoordinate += 1.8 * sin(u * 2.0) + current.x * 45.0;
        crossCoordinate = uv.x * 46.0 + 0.42 * sin(v * 3.0);
        relief = 0.025 * sin(contour * auroraSurfaceTau)
               + 0.075 * (density - lowerDensity);
        palettePosition = 0.5 + 0.34 * sin(contour * 0.72 + world.x);
    } else if (mode == 1) {
        // Liquid Aperture: circumferential nacre strata with radial tool marks.
        contour = uv.y * 7.0 + 0.33 * sin(u * 3.0 + phase * 0.08)
                + density * 2.8;
        fineCoordinate = uv.y * 96.0 + 1.7 * sin(u * 5.0) + density * 5.0;
        crossCoordinate = uv.x * 132.0 + 0.65 * sin(v * 2.0);
        relief = 0.018 * sin(contour * auroraSurfaceTau) + density * 0.065;
        palettePosition = 0.5 + 0.33 * sin(v + 0.8 * sin(u) + density * 3.0);
        lightTrace = auroraSurfaceLine(uv.y * 12.0 + 0.14 * sin(u * 6.0), 0.035);
        glazing = 0.74;
    } else if (mode == 3) {
        // Night Transit: glass terraces, narrow window rows and rainlike reflections.
        contour = uv.y * 8.0 + density * 2.4 + 0.18 * sin(u * 2.0);
        fineCoordinate = uv.y * 136.0;
        crossCoordinate = uv.x * 40.0;
        float facade = auroraSurfaceLine(crossCoordinate, 0.045);
        float floorLine = auroraSurfaceLine(uv.y * 34.0, 0.050);
        float lane = auroraSurfaceWave(uv.x * 20.0 + 0.10 * sin(v));
        lightTrace = floorLine * pow(lane, 4.0) * (0.14 + 0.52 * spectral + pulse * 0.34);
        relief = (facade + floorLine) * 0.002 + density * 0.035;
        palettePosition = clamp(0.12 + spectral * 0.46 + density * 0.8, 0.0, 1.0);
        glazing = 0.90;
    } else if (mode == 4) {
        // Diffraction Veil: two optical weaves with broad beat fringes between them.
        fineCoordinate = uv.x * 84.0 + 1.9 * sin(v * 3.0) + density * 4.0;
        crossCoordinate = uv.y * 78.0 - 1.3 * cos(u * 2.0) - lowerDensity * 4.0;
        contour = uv.x * 5.0 - uv.y * 4.0 + density * 2.5
                + 0.34 * sin(v * 2.0 + phase * 0.09);
        relief = 0.018 * sin(contour * auroraSurfaceTau) + density * 0.06;
        palettePosition = 0.5 + 0.37 * sin(contour * 1.35);
        lightTrace = auroraSurfaceLine(contour * 2.0, 0.026);
        glazing = 0.88;
    } else {
        // Orbit Loom: helical cable winding and separated luminous internal strands.
        contour = uv.y * 8.0 + uv.x * 3.0 + 0.24 * sin(u * 3.0) + density * 2.6;
        fineCoordinate = uv.y * 104.0 + uv.x * 18.0 + density * 5.0;
        crossCoordinate = uv.x * 92.0 + 0.36 * sin(v * 3.0);
        relief = 0.020 * sin(contour * auroraSurfaceTau) + density * 0.05;
        palettePosition = 0.5 + 0.34 * sin(v * 2.0 + u * 3.0 + density * 4.0);
        lightTrace = auroraSurfaceLine(uv.y * 5.0 + uv.x * 2.0, 0.024);
        glazing = 0.78;
    }

    float lamina = auroraSurfaceWave(contour);
    float etched = auroraSurfaceLine(fineCoordinate, 0.033);
    float crossing = auroraSurfaceLine(crossCoordinate, 0.022);
    float etchWeight = mode == 4 ? max(etched, crossing) : etched * 0.76 + crossing * 0.24;
    relief += (lamina - 0.5) * 0.007;

    // Screen-space surface-gradient bump mapping remains attached to the geometry.
    // Cap the gradient so a sharp pigment front cannot flip the surface normal.
    vec3 dx = dFdx(world), dy = dFdy(world);
    vec3 rx = cross(dy, n), ry = cross(n, dx);
    float determinant = dot(dx, rx);
    vec3 slope = (dFdx(relief) * rx + dFdy(relief) * ry)
               / max(abs(determinant), 0.0000001);
    slope *= sign(determinant);
    slope /= max(1.0, length(slope) * 1.8);
    n = auroraSurfaceNormal(n - slope * 0.40, n);

    vec3 tint = auroraSurfacePalette(palettePosition, a, b, c);
    vec3 surfacePigment = auroraSurfacePigmentColour(pigment, a, b, c);
    vec3 submerged = auroraSurfacePigmentColour(lowerPigment, a, b, c);
    tint = mix(tint, surfacePigment, pigmentCoverage * 0.50);
    tint = mix(tint, submerged, (0.08 + 0.12 * lamina) * glazing);

    vec3 key = normalize(vec3(-0.65, 0.82, 1.25));
    vec3 fill = normalize(vec3(0.90, -0.20, 0.65));
    float diffuse = max(dot(n, key), 0.0);
    float backLight = max(dot(-n, fill), 0.0);
    float facing = clamp(abs(dot(n, eye)), 0.0, 1.0);
    float fresnel = pow(1.0 - facing, 2.7);
    vec3 reflected = reflect(-eye, n);
    float softbox = exp(-pow((reflected.x + 0.32) * 3.2, 2.0))
                 * smoothstep(-0.48, 0.62, reflected.y);
    float edgebox = exp(-pow((reflected.x - 0.70) * 8.5, 2.0))
                 * (0.35 + 0.65 * max(reflected.y, 0.0));
    float shininess = theme == 2 ? 92.0 : (theme == 1 || theme == 3) ? 58.0 : 34.0;
    vec3 halfVector = auroraSurfaceNormal(key + eye, key);
    float pinLight = pow(max(dot(n, halfVector), 0.0), shininess);
    float thinFilm = auroraSurfaceWave((1.0 - facing) * 3.4 + density * 5.0 + lamina * 0.35);
    vec3 opticalTint = auroraSurfacePalette(0.14 + thinFilm * 0.72, a, b, c);

    // Keep substantial coloured midtones and shadows. Reflections are bounded
    // highlights; no global whitening or rainbow palette can replace custom RGB.
    float ambient = (theme == 1 || theme == 3) ? 0.19 : theme == 2 ? 0.24 : 0.36;
    vec3 colour = tint * (ambient + diffuse * 0.70);
    colour *= 0.84 + lamina * 0.16;
    colour = mix(colour, tint * 0.32, etchWeight * 0.23);
    colour += submerged * backLight * glazing * 0.15;
    colour = mix(colour, opticalTint * (0.56 + diffuse * 0.42), fresnel * 0.30);
    vec3 reflectionColour = mix(max(b, c), vec3(1.0), theme == 2 ? 0.30 : 0.14);
    float coat = softbox * 0.24 + edgebox * 0.19 + pinLight * (theme == 2 ? 0.62 : 0.38);
    colour += reflectionColour * coat * (0.65 + lamina * 0.35);
    colour += opticalTint * fresnel * (theme == 3 ? 0.32 : theme == 1 ? 0.42 : 0.20);
    colour += c * etchWeight * pinLight * (0.09 + 0.05 * drive.z);
    colour += b * lightTrace * (theme == 3 ? 0.17 : theme == 1 ? 0.24 : 0.085)
            * (0.35 + 0.30 * drive.z + pulse * 0.55);
    if (theme == 1 || theme == 3)
        colour += c * pow(clamp(spectral, 0.0, 1.0), 3.0) * fresnel * 0.20;
    if (theme == 2)
        colour += reflectionColour * auroraSurfaceLine(fineCoordinate * 0.5, 0.02)
                * softbox * 0.13;
    return clamp(colour, vec3(0.0), vec3(2.5));
}
)GLSL";
