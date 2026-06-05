// src/game/Camera.h
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

class Camera {
public:
    Camera(float fov, float aspect, float nearClip, float farClip)
        : m_fov(fov), m_aspect(aspect), m_near(nearClip), m_far(farClip) {
        updateProjection();
    }

    void setAspectRatio(float aspect) {
        m_aspect = aspect;
        updateProjection();
    }

    // Update camera from target position and rotation each frame.
    void update(const glm::vec3& target, float orbitAngleDegrees, float dt) {
        if (!m_hasFollowTarget) {
            snapToTarget(target, orbitAngleDegrees);
            return;
        }

        const float alpha = 1.0f - std::exp(-m_followSharpness * dt);
        m_followTarget += (target - m_followTarget) * alpha;
        updateView(orbitAngleDegrees);
    }

    void snapToTarget(const glm::vec3& target, float orbitAngleDegrees) {
        m_followTarget = target;
        m_hasFollowTarget = true;
        updateView(orbitAngleDegrees);
    }

    // Adjust the orbit (chase) distance — driven by the scroll wheel. Positive delta
    // pulls the camera out, negative pushes it in; clamped to a usable range.
    void zoom(float delta) {
        m_orbitDistance = glm::clamp(m_orbitDistance + delta, kMinOrbitDistance, kMaxOrbitDistance);
    }

    const glm::mat4& view() const { return m_view; }
    const glm::mat4& proj() const { return m_proj; }
    glm::mat4 viewProj() const { return m_proj * m_view; }
    const glm::vec3& position() const { return m_position; }
    float fov() const { return m_fov; }       // vertical FOV in degrees
    float aspect() const { return m_aspect; } // width / height

private:
    void updateView(float orbitAngleDegrees) {
        float rad = glm::radians(orbitAngleDegrees);
        float pitch = glm::radians(m_orbitPitch);

        m_position = m_followTarget + glm::vec3{
            m_orbitDistance * glm::cos(pitch) * glm::cos(rad),
            m_orbitDistance * glm::cos(pitch) * glm::sin(rad),
            m_orbitDistance * glm::sin(pitch)
        };

        m_view = glm::lookAt(m_position, m_followTarget, glm::vec3{ 0.0f, 0.0f, 1.0f });
    }

    void updateProjection() {
        m_proj = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
        m_proj[1][1] *= -1;
    }

    glm::mat4 m_view{ 1.0f };
    glm::mat4 m_proj{ 1.0f };
    glm::vec3 m_position{ 0.0f, 0.0f, 0.0f };
    glm::vec3 m_followTarget{ 0.0f, 0.0f, 0.0f };
    bool m_hasFollowTarget = false;

    float m_fov;
    float m_aspect;
    float m_near;
    float m_far;

    // Ship-chase view: calibrated for a ship-scale hero asset instead of the original
    // one-tile farm avatar, so ocean texels are judged from a real naval-game distance.
    static constexpr float kMinOrbitDistance = 42.0f;
    static constexpr float kMaxOrbitDistance = 180.0f;
    float m_orbitDistance = 90.0f;
    float m_orbitPitch = 18.0f;
    float m_followSharpness = 9.0f;
};
