#pragma once
#include <glm/glm.hpp>

class Player {
public:
    const glm::vec3& position() const { return m_position; }
    const glm::vec2& facingDirection() const { return m_facingDirection; }
    float moveSpeed() const { return m_moveSpeed; }

    void moveBy(const glm::vec2& delta) {
        m_position.x += delta.x;
        m_position.y += delta.y;
    }

    void setPosition(const glm::vec3& pos) { m_position = pos; }

    void setFacingDirection(const glm::vec2& direction) {
        m_facingDirection = direction;
    }

private:
    glm::vec3 m_position{15.0f, 15.0f, 1.0f};
    glm::vec2 m_facingDirection{0.0f, -1.0f};
    float     m_moveSpeed = 3.0f;
};
