#pragma once

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/math.hpp"
#include "core/types.hpp"

namespace mc {

// First-person camera (PHASE3 §1.2 / PHASE4). Position + yaw/pitch.
class Camera {
public:
    Vec3 position{0, 0, 0};
    float yaw = 0.0f;   // radians; 0 -> -Z
    float pitch = 0.0f; // radians; +up
    float fov = 70.0f;
    float aspect = 16.0f / 9.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;

    // View bobbing offsets
    Vec3 view_offset{0, 0, 0};
    float roll = 0.0f;       // radians
    float bob_pitch = 0.0f;  // radians

    [[nodiscard]] Vec3 forward() const { return forward_from_yaw_pitch(yaw, pitch); }
    // Right vector = cross(forward, up), flattened to XZ (perpendicular to
    // forward on the horizontal plane). yaw=0 -> forward +Z, right -X.
    [[nodiscard]] Vec3 right() const {
        return Vec3(-std::cos(yaw), 0.0f, -std::sin(yaw));
    }
    [[nodiscard]] Vec3 up() const { return glm::cross(right(), forward()); }

    [[nodiscard]] glm::mat4 view() const {
        // 1. Base rotation matrix (camera at origin)
        glm::mat4 rot = glm::lookAt(glm::vec3(0.0f), glm::vec3(forward()), glm::vec3(0.0f, 1.0f, 0.0f));
        
        // 2. Bobbing matrix (applied in view space)
        glm::mat4 bob(1.0f);
        bob = glm::translate(bob, glm::vec3(view_offset.x, view_offset.y, view_offset.z));
        bob = glm::rotate(bob, roll, glm::vec3(0.0f, 0.0f, 1.0f));
        bob = glm::rotate(bob, bob_pitch, glm::vec3(1.0f, 0.0f, 0.0f));

        // 3. Translation matrix (world position)
        glm::mat4 trans = glm::translate(glm::mat4(1.0f), glm::vec3(-position.x, -position.y, -position.z));
        
        return bob * rot * trans;
    }
    [[nodiscard]] glm::mat4 projection() const {
        return glm::perspective(glm::radians(fov), aspect, near_plane, far_plane);
    }
    [[nodiscard]] glm::mat4 view_projection() const { return projection() * view(); }

    [[nodiscard]] bool sphere_in_frustum(Vec3 center, float radius) const {
        glm::mat4 vp = view_projection();
        glm::vec4 rows[4] = {
            glm::vec4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]), // row 0
            glm::vec4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]), // row 1
            glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]), // row 2
            glm::vec4(vp[0][3], vp[1][3], vp[2][3], vp[3][3])  // row 3
        };
        glm::vec4 planes[6] = {
            rows[3] + rows[0], // left
            rows[3] - rows[0], // right
            rows[3] + rows[1], // bottom
            rows[3] - rows[1], // top
            rows[3] + rows[2], // near
            rows[3] - rows[2], // far
        };
        for (int i = 0; i < 6; ++i) {
            float len = std::sqrt(planes[i].x * planes[i].x + planes[i].y * planes[i].y + planes[i].z * planes[i].z);
            if (len == 0.0f) continue;
            float d = (planes[i].x * center.x + planes[i].y * center.y + planes[i].z * center.z + planes[i].w) / len;
            if (d < -radius) return false;
        }
        return true;
    }

    // AABB frustum culling
    [[nodiscard]] bool aabb_in_frustum(Vec3 min, Vec3 max) const {
        glm::mat4 vp = view_projection();
        glm::vec4 rows[4] = {
            glm::vec4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]),
            glm::vec4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]),
            glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]),
            glm::vec4(vp[0][3], vp[1][3], vp[2][3], vp[3][3])
        };
        glm::vec4 planes[6] = {
            rows[3] + rows[0], // left
            rows[3] - rows[0], // right
            rows[3] + rows[1], // bottom
            rows[3] - rows[1], // top
            rows[3] + rows[2], // near
            rows[3] - rows[2], // far
        };
        for (int i = 0; i < 6; ++i) {
            float len = std::sqrt(planes[i].x * planes[i].x + planes[i].y * planes[i].y + planes[i].z * planes[i].z);
            if (len == 0.0f) continue;
            planes[i] /= len; // normalize
            
            // Find the positive vertex (p-vertex) of the AABB along the plane normal
            Vec3 p_vertex = min;
            if (planes[i].x >= 0.0f) p_vertex.x = max.x;
            if (planes[i].y >= 0.0f) p_vertex.y = max.y;
            if (planes[i].z >= 0.0f) p_vertex.z = max.z;

            // If the p-vertex is on the negative side of the plane, AABB is completely outside
            if (planes[i].x * p_vertex.x + planes[i].y * p_vertex.y + planes[i].z * p_vertex.z + planes[i].w < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

} // namespace mc
