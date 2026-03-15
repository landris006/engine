#pragma once

#include <fstream>
#include <glm/glm.hpp>
#include <sstream>

struct SceneConfig {
  glm::vec3 camera_pos = glm::vec3(0.0, 2.0, 10.0);
  float camera_yaw_deg = -90.0f;
  float camera_pitch_deg = 0.0f;
  float camera_vfov_deg = 40.0f;
};

static SceneConfig load_scene_ini(const char* path = "scene.ini") {
  SceneConfig cfg;
  std::ifstream file(path);

  if (!file.is_open()) {
    return cfg;
  }

  std::string line;
  std::string section;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == ';' || line[0] == '#') {
      continue;
    }

    if (line[0] == '[') {
      section = line.substr(1, line.size() - 2);
      continue;
    }

    if (section == "camera") {
      auto pos = line.find('=');
      if (pos == std::string::npos) {
        continue;
      }

      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos + 1, line.size() - pos - 1);

      if (key == "pos") {
        std::istringstream pos_stream(value);
        std::string token;
        if (std::getline(pos_stream, token, ','))
          cfg.camera_pos.x = std::stof(token);
        if (std::getline(pos_stream, token, ','))
          cfg.camera_pos.y = std::stof(token);
        if (std::getline(pos_stream, token, ','))
          cfg.camera_pos.z = std::stof(token);
      }

      if (key == "pitch") cfg.camera_pitch_deg = std::stof(value);
      if (key == "yaw") cfg.camera_yaw_deg = std::stof(value);
      if (key == "vfov") cfg.camera_vfov_deg = std::stof(value);
    }
  }

  return cfg;
}
