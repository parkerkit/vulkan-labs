#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

#include "aabb.h"

namespace fw {
struct Vertex {
  glm::vec3 positions{};
  glm::vec3 normals{};
  glm::vec2 texcoords{};
};

struct Mesh {
  std::vector<Vertex> vertices{};
  std::vector<std::uint32_t> indices{};
  AABB bounds{};
};
}  // namespace fw
