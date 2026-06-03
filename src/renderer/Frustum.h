#pragma once
#include <glm/glm.hpp>
#include <array>

struct Frustum {
    std::array<glm::vec4, 6> planes; // Inside if ax + by + cz + d >= 0

    // Gribb & Hartmann — extract 6 planes from viewProj matrix
    static Frustum extractFrom(const glm::mat4& m) {
        // GLM uses column-major: m[col][row]
        auto row = [&](int r) {
            return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]);
        };
        Frustum f;
        f.planes[0] = row(3) + row(0); // left
        f.planes[1] = row(3) - row(0); // right
        f.planes[2] = row(3) + row(1); // bottom
        f.planes[3] = row(3) - row(1); // top
        f.planes[4] = row(2);          // near (Vulkan [0,1] depth — GLM_FORCE_DEPTH_ZERO_TO_ONE)
        f.planes[5] = row(3) - row(2); // far
        return f;
    }

    // True if AABB is inside frustum
    // False if AABB is outside any plane (culled)
    bool containsAABB(const glm::vec3& min, const glm::vec3& max) const {
        for (const auto& plane : planes) {
            glm::vec3 n(plane);
            // Farthest vertex along plane normal (positive vertex)
            glm::vec3 p = {
                n.x >= 0.0f ? max.x : min.x,
                n.y >= 0.0f ? max.y : min.y,
                n.z >= 0.0f ? max.z : min.z
            };
            if (glm::dot(n, p) + plane.w < 0.0f) return false;
        }
        return true;
    }
};
