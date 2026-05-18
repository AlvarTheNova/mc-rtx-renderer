#pragma once

// Phase 1.4.5 — view-frustum culling for chunk draws.
//
// Standard Gribb-Hartmann plane extraction from a column-major view*proj
// matrix. The 6 derived planes have the form (ax + by + cz + d = 0) with
// normals pointing INWARD; a world-space point P is inside the frustum
// iff dot(plane.xyz, P) + plane.w >= 0 for all 6 planes.
//
// Reference:
//   Gribb & Hartmann, "Fast Extraction of Viewing Frustum Planes from the
//   World-View-Projection Matrix" (2001).
//
// We use the MC-shipped proj * view directly (no GL→VK clip applied — that
// matrix lives in the vertex shader and only remaps clip-space coords, not
// the world-space visibility envelope).

#include <cmath>
#include <cstdint>

namespace rtxmc {

struct FrustumPlanes {
    // [0]=L, [1]=R, [2]=B, [3]=T, [4]=N, [5]=F. Each plane: (nx, ny, nz, d).
    float planes[6][4];
};

namespace detail {

// Column-major matrix multiply: out = a * b. Both stored as col[0..3].
inline void mat4_mul(const float a[16], const float b[16], float out[16]) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a[k * 4 + row] * b[col * 4 + k];
            }
            out[col * 4 + row] = s;
        }
    }
}

inline float row(const float m[16], int r, int c) {
    // column-major: m[col*4 + row]
    return m[c * 4 + r];
}

inline void normalize_plane(float p[4]) {
    float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    if (len > 0.0f) {
        float inv = 1.0f / len;
        p[0] *= inv; p[1] *= inv; p[2] *= inv; p[3] *= inv;
    }
}

} // namespace detail

inline void extract_frustum_planes(const float view[16],
                                   const float proj[16],
                                   FrustumPlanes& out) {
    float vp[16];
    detail::mat4_mul(proj, view, vp); // column-major: VP = P * V

    // Gribb-Hartmann: each plane = row3 ± rowK of VP. The rows here are
    // logical matrix rows (row r = vp[c*4 + r] for c=0..3), not the
    // contiguous 4-floats of column-major storage.
    auto R = [&](int r, int c) { return detail::row(vp, r, c); };

    // Left:   row3 + row0
    out.planes[0][0] = R(3,0) + R(0,0);
    out.planes[0][1] = R(3,1) + R(0,1);
    out.planes[0][2] = R(3,2) + R(0,2);
    out.planes[0][3] = R(3,3) + R(0,3);
    // Right:  row3 - row0
    out.planes[1][0] = R(3,0) - R(0,0);
    out.planes[1][1] = R(3,1) - R(0,1);
    out.planes[1][2] = R(3,2) - R(0,2);
    out.planes[1][3] = R(3,3) - R(0,3);
    // Bottom: row3 + row1
    out.planes[2][0] = R(3,0) + R(1,0);
    out.planes[2][1] = R(3,1) + R(1,1);
    out.planes[2][2] = R(3,2) + R(1,2);
    out.planes[2][3] = R(3,3) + R(1,3);
    // Top:    row3 - row1
    out.planes[3][0] = R(3,0) - R(1,0);
    out.planes[3][1] = R(3,1) - R(1,1);
    out.planes[3][2] = R(3,2) - R(1,2);
    out.planes[3][3] = R(3,3) - R(1,3);
    // Near:   row3 + row2  (OpenGL convention; works for VK with GL-style proj)
    out.planes[4][0] = R(3,0) + R(2,0);
    out.planes[4][1] = R(3,1) + R(2,1);
    out.planes[4][2] = R(3,2) + R(2,2);
    out.planes[4][3] = R(3,3) + R(2,3);
    // Far:    row3 - row2
    out.planes[5][0] = R(3,0) - R(2,0);
    out.planes[5][1] = R(3,1) - R(2,1);
    out.planes[5][2] = R(3,2) - R(2,2);
    out.planes[5][3] = R(3,3) - R(2,3);

    for (int i = 0; i < 6; ++i) detail::normalize_plane(out.planes[i]);
}

// Returns true if the AABB (min..max) lies entirely OUTSIDE the frustum
// (and therefore can be culled). Uses the standard "n-vertex" test: for each
// plane, pick the AABB corner farthest along the plane normal; if THAT
// corner is on the negative side, the whole AABB is outside.
inline bool aabb_outside_frustum(const FrustumPlanes& f,
                                 const float mn[3], const float mx[3]) {
    for (int i = 0; i < 6; ++i) {
        const float a = f.planes[i][0];
        const float b = f.planes[i][1];
        const float c = f.planes[i][2];
        const float d = f.planes[i][3];
        // Pick the corner maximising dot(normal, corner)
        const float px = (a >= 0.0f) ? mx[0] : mn[0];
        const float py = (b >= 0.0f) ? mx[1] : mn[1];
        const float pz = (c >= 0.0f) ? mx[2] : mn[2];
        if (a * px + b * py + c * pz + d < 0.0f) return true; // outside
    }
    return false;
}

} // namespace rtxmc
