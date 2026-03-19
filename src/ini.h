#pragma once

#include <glm/glm.hpp>

struct SceneConfig {
  glm::vec3 camera_pos = glm::vec3(0.0, 2.0, 10.0);
  float camera_yaw_deg = -90.0f;
  float camera_pitch_deg = 0.0f;
  float camera_vfov_deg = 40.0f;
};

SceneConfig load_scene_ini(const char* path = "scene.ini");
bool save_scene_ini(const SceneConfig& cfg, const char* path = "scene.ini");
