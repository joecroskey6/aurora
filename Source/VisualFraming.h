#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Perspective framing for the six parametric meshes in VisualSurface.cpp.
// Keep their rotations, translations and displacement limits in sync here.
// The fit changes only the projection; lighting still uses the original world.
struct VisualFraming
{
    float distance = 3.5f;
    float fit = 1.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float zBound = 1.0f;
};

namespace visual_framing_detail
{
struct Point
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline double nonnegative(float value)
{
    return std::isfinite(value) ? std::max(0.0, double(value)) : 0.0;
}

inline Point rotateX(Point p, double angle)
{
    const double c = std::cos(angle), s = std::sin(angle);
    return {p.x, c * p.y - s * p.z, s * p.y + c * p.z};
}

inline Point rotateY(Point p, double angle)
{
    const double c = std::cos(angle), s = std::sin(angle);
    return {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
}

inline Point rotateZ(Point p, double angle)
{
    const double c = std::cos(angle), s = std::sin(angle);
    return {c * p.x - s * p.y, s * p.x + c * p.y, p.z};
}

inline Point scale(Point p, double amount)
{
    return {p.x * amount, p.y * amount, p.z * amount};
}

inline std::array<Point, 8> enclosingCorners(int mode, float phase, float gain,
                                          float low, float mid, float high,
                                          float correlation, float maxBand,
                                          float maxLeft, float maxRight)
{
    const double g = std::isfinite(gain) ? std::clamp(double(gain), 0.25, 4.0) : 1.0;
    const double strength = std::min(g, 2.6);
    const double b = nonnegative(maxBand);
    const double left = nonnegative(maxLeft), right = nonnegative(maxRight);
    const double bass = nonnegative(low), middle = nonnegative(mid), air = nonnegative(high);
    const double corr = std::isfinite(correlation) ? std::clamp(double(correlation), -1.0, 1.0) : 0.0;
    const double time = std::isfinite(phase) ? double(phase) : 0.0;
    const double attenuation = 1.0 / (1.0 + std::max(0.0, g - 1.0) * 0.08);

    Point half;
    double centerZ = 0.0;
    switch (mode)
    {
        case 0:
            half = {1.675, 1.155, 0.40 + 0.36 * g * b + 0.09 * g * left};
            break;
        case 1:
        {
            const double radius = 1.11 + 0.13 * g * b + 0.065 * middle * strength;
            half = {radius * (1.0 + 0.12 * (1.0 - corr)), radius,
                    0.27 + 0.14 * g * left};
            break;
        }
        case 2:
        {
            const double radius = 1.05 + 0.19 * g * b;
            half = {radius + 0.16 * bass * strength, radius, radius};
            break;
        }
        case 3:
        {
            // Transit's displacement is one-sided before its -0.3 offset.
            const double height = 0.85 * b * strength;
            half = {1.6, 1.4, height * 0.5};
            centerZ = height * 0.5 - 0.3;
            break;
        }
        case 4:
            half = {1.6, 1.05, 0.35 + 0.26 * g * b
                                      + 0.10 * g * left + 0.15 * g * right};
            break;
        default:
        {
            // The knot's normal/binormal pair is orthonormal, so its tube
            // contributes at most its radius to any coordinate.
            const double tube = 0.12 + 0.065 * g * b + 0.025 * air * strength;
            half = {(1.01 + tube) * (1.0 + 0.10 * (1.0 - corr)),
                    1.01 + tube, 0.32 + tube};
            break;
        }
    }

    std::array<Point, 8> corners;
    for (int i = 0; i < 8; ++i)
    {
        Point p {(i & 1) ? half.x : -half.x,
                 (i & 2) ? half.y : -half.y,
                 centerZ + ((i & 4) ? half.z : -half.z)};
        switch (mode)
        {
            case 0: p = scale(rotateZ(rotateX(p, 0.63), -0.22), 0.90); break;
            case 1: p = rotateZ(rotateX(p, 0.54), 0.20); break;
            case 2: p = rotateY(p, time * 0.09 + 0.60); break;
            case 3:
                p = scale(rotateZ(rotateX(p, -0.90), -0.26), 0.80);
                p.y -= 0.08;
                break;
            case 4: p = scale(rotateZ(rotateX(p, 0.34), 0.20), 0.82); break;
            default: p = rotateY(rotateX(p, 0.38), 0.30 + time * 0.04); break;
        }
        corners[std::size_t(i)] = scale(p, attenuation);
    }
    return corners;
}
} // namespace visual_framing_detail

// maxBand/maxLeft/maxRight must bound the SAME copied scene sent to the GPU.
// Waveform maxima may exceed unity; this helper does not clip those bounds.
// With positive perspective denominators, projected extrema of an enclosing
// box occur at its eight corners. Fitting them therefore encloses the whole
// rendered mesh, including triangle interiors, at every aspect ratio.
//
// Vertex projection (pass 1 only), replacing the old fixed 2.75 focal length:
//   float w = framing.distance - p.z;
//   vec2 xy = framing.fit * (vec2(p.x / aspect, p.y)
//                           - vec2(framing.centerX, framing.centerY) * w);
//   float nearPlane = 0.1;
//   float farPlane = framing.distance + framing.zBound + 1.0;
//   float z = ((farPlane + nearPlane) * w - 2.0 * farPlane * nearPlane)
//             / (farPlane - nearPlane);
//   gl_Position = vec4(xy, z, w);
// This is ordinary perspective depth with safe near/far planes. Use the same
// distance for the fragment shader's eye position. Do not also multiply 2.75.
inline VisualFraming computeVisualFraming(int mode, float aspect, float phase,
                                         float gain, float low, float mid,
                                         float high, float correlation,
                                         float maxBand, float maxLeft,
                                         float maxRight, float yaw = 0, float pitch = 0)
{
    auto corners = visual_framing_detail::enclosingCorners(
        mode, phase, gain, low, mid, high, correlation, maxBand, maxLeft, maxRight);
    for (auto& point : corners)
        point = visual_framing_detail::rotateX(visual_framing_detail::rotateY(point, yaw), pitch);
    const double ratio = std::isfinite(aspect) && aspect > 0.0f ? double(aspect) : 1.0;
    double maxZ = -std::numeric_limits<double>::infinity();
    double zBound = 1.0;
    for (const auto& p : corners)
    {
        maxZ = std::max(maxZ, p.z);
        zBound = std::max(zBound, std::abs(p.z));
    }
    // Round to the actual float uniform before fitting. Otherwise a rounded
    // camera distance can subtly invalidate bounds computed in double.
    const float distance = float(std::max(3.5, maxZ + 1.0));
    double minX = std::numeric_limits<double>::infinity(), minY = minX;
    double maxX = -minX, maxY = -minX;
    for (const auto& p : corners)
    {
        const double denominator = double(distance) - p.z;
        const double x = p.x / (ratio * denominator), y = p.y / denominator;
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    const float centerX = float((minX + maxX) * 0.5);
    const float centerY = float((minY + maxY) * 0.5);
    const double extent = std::max({std::abs(minX - centerX), std::abs(maxX - centerX),
                                    std::abs(minY - centerY), std::abs(maxY - centerY), 1.0e-9});
    // +/-0.86 NDC leaves 7% of the viewport at each edge of the limiting axis.
    return {distance, float(0.86 / extent), centerX, centerY, float(zBound * 1.001)};
}
