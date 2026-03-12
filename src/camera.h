#pragma once

#ifndef HEADLESS
#include <GLFW/glfw3.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

struct CameraUbo {
  glm::vec4 origin;
  glm::vec4 lower_left;
  glm::vec4 horizontal;
  glm::vec4 vertical;
};

class Camera {
 public:
  glm::vec3 position = glm::vec3();
  float yaw = -glm::half_pi<float>();  // -90° → forward along -Z
  float pitch = 0.0f;
  float fov = 40.0f;    // vertical fov in degrees
  float aspect = 1.6f;  // width / height

  CameraUbo create_ubo() const {
    glm::vec3 forward = glm::normalize(
        glm::vec3(glm::cos(yaw) * glm::cos(pitch), glm::sin(pitch),
                  glm::sin(yaw) * glm::cos(pitch)));
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    float half_h = glm::tan(glm::radians(fov) / 2.0f);
    float half_w = aspect * half_h;

    glm::vec3 center = position + forward;
    glm::vec3 lower_left = center - right * half_w - up * half_h;
    glm::vec3 horizontal = right * 2.0f * half_w;
    glm::vec3 vertical = up * 2.0f * half_h;

    return CameraUbo{
        .origin = glm::vec4(position, 0),
        .lower_left = glm::vec4(lower_left, 0),
        .horizontal = glm::vec4(horizontal, 0),
        .vertical = glm::vec4(vertical, 0),
    };
  }
};

#ifndef HEADLESS
class CameraController {
 public:
  virtual ~CameraController() = default;
  virtual bool update(Camera& cam, GLFWwindow* window, float dt) = 0;
};

class FpsCameraController : public CameraController {
 public:
  float speed = 3.0f;        // world units/second
  float sensitivity = 0.1f;  // degrees/pixel

 private:
  double prev_x = 0;
  double prev_y = 0;
  bool right_pressed = false;

 public:
  bool update(Camera& cam, GLFWwindow* window, float dt) override {
    bool moved = false;

    // Mouse look — only while right button held
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
      if (!right_pressed) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        this->right_pressed = true;
      } else {
        float dx = (float)(x - prev_x) * sensitivity;
        float dy = (float)(y - prev_y) * sensitivity;
        cam.yaw += glm::radians(dx);
        cam.pitch -= glm::radians(dy);
        cam.pitch =
            glm::clamp(cam.pitch, glm::radians(-89.0f), glm::radians(89.0f));
        if (dx != 0 || dy != 0) {
          moved = true;
        }

        glfwSetCursorPos(window, x, y);
      }
      prev_x = x;
      prev_y = y;
    } else {
      right_pressed = false;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    // WASD + E/Space (up) + Q/Shift (down)
    glm::vec3 forward = glm::normalize(
        glm::vec3(glm::cos(cam.yaw) * glm::cos(cam.pitch), glm::sin(cam.pitch),
                  glm::sin(cam.yaw) * glm::cos(cam.pitch)));
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

    glm::vec3 move(0);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
      move += forward;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
      move -= forward;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
      move -= right;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
      move += right;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
      move.y += 1;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
      move.y -= 1;
    }

    if (glm::length(move) > 0.0f) {
      cam.position += glm::normalize(move) * speed * dt;
      moved = true;
    }

    return moved;
  }
};
#endif  // HEADLESS
