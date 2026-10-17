#include "framework/shapes.h"

#include <array>
#include <numbers>

namespace fw {
Mesh Shapes::Cube(float size, const glm::vec3& center) {
  Mesh mesh{};
  if (size <= 0.0F) {
    return mesh;
  }

  const float half = size * 0.5F;
  constexpr size_t vertex_count = 24;

  // 局部顶点坐标定义（以原点为中心），前后左右上下
  std::array<glm::vec3, vertex_count> local_positions = {
    {{-half, -half, half},  {half, -half, half},  {half, half, half},  {-half, half, half},   {half, -half, -half},
     {-half, -half, -half}, {-half, half, -half}, {half, half, -half}, {-half, -half, -half}, {-half, -half, half},
     {-half, half, half},   {-half, half, -half}, {half, -half, half}, {half, -half, -half},  {half, half, -half},
     {half, half, half},    {-half, half, half},  {half, half, half},  {half, half, -half},   {-half, half, -half},
     {-half, -half, -half}, {half, -half, -half}, {half, -half, half}, {-half, -half, half}}};

  // 法线方向不受平移和等比缩放影响
  std::array<glm::vec3, vertex_count> normals = {
    {{0.0F, 0.0F, 1.0F},  {0.0F, 0.0F, 1.0F},  {0.0F, 0.0F, 1.0F},  {0.0F, 0.0F, 1.0F},  {0.0F, 0.0F, -1.0F},
     {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, -1.0F}, {-1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F},
     {-1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},  {1.0F, 0.0F, 0.0F},  {1.0F, 0.0F, 0.0F},
     {1.0F, 0.0F, 0.0F},  {0.0F, 1.0F, 0.0F},  {0.0F, 1.0F, 0.0F},  {0.0F, 1.0F, 0.0F},  {0.0F, 1.0F, 0.0F},
     {0.0F, -1.0F, 0.0F}, {0.0F, -1.0F, 0.0F}, {0.0F, -1.0F, 0.0F}, {0.0F, -1.0F, 0.0F}}};

  std::array<glm::vec2, vertex_count> texcoords = {
    {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F},
     {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F},
     {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}, {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}};

  mesh.vertices.resize(local_positions.size());
  for (std::size_t i = 0; i < local_positions.size(); ++i) {
    mesh.vertices[i].positions = local_positions[i] + center;
    mesh.vertices[i].normals = normals[i];
    mesh.vertices[i].texcoords = texcoords[i];
  }

  for (std::uint32_t i = 0; i < 6; ++i) {
    const std::uint32_t offset = i * 4;
    mesh.indices.push_back(offset);
    mesh.indices.push_back(offset + 1);
    mesh.indices.push_back(offset + 2);
    mesh.indices.push_back(offset + 2);
    mesh.indices.push_back(offset + 3);
    mesh.indices.push_back(offset);
  }

  mesh.bounds = {glm::vec3{-half} + center, glm::vec3{half} + center};

  return mesh;
}

Mesh Shapes::Sphere(float radius, std::uint32_t x_segments, std::uint32_t y_segments, const glm::vec3& center) {
  Mesh mesh{};
  if (radius <= 0.0F || x_segments < 3 || y_segments < 2) {
    return mesh;
  }

  for (std::uint32_t y = 0; y <= y_segments; ++y) {
    const float y_segment = static_cast<float>(y) / static_cast<float>(y_segments);
    const float y_angle = y_segment * std::numbers::pi_v<float>;

    for (std::uint32_t x = 0; x <= x_segments; ++x) {
      const float x_segment = static_cast<float>(x) / static_cast<float>(x_segments);
      const float x_angle = x_segment * 2.0F * std::numbers::pi_v<float>;

      // 基于单位球计算方向单位向量
      const float x_dir = std::cos(x_angle) * std::sin(y_angle);
      const float y_dir = std::cos(y_angle);
      const float z_dir = std::sin(x_angle) * std::sin(y_angle);

      const glm::vec3 normal_direction{x_dir, y_dir, z_dir};

      Vertex vertex{};
      vertex.positions = normal_direction * radius + center;
      vertex.normals = normal_direction;
      vertex.texcoords = {x_segment, y_segment};

      mesh.vertices.emplace_back(vertex);
    }
  }

  for (std::uint32_t y = 0; y < y_segments; ++y) {
    for (std::uint32_t x = 0; x < x_segments; ++x) {
      const std::uint32_t current = y * (x_segments + 1) + x;
      const std::uint32_t next = current + x_segments + 1;

      // 从球外观察时使用逆时针绕序，使几何法线与顶点的向外法线一致。
      mesh.indices.push_back(current);
      mesh.indices.push_back(current + 1);
      mesh.indices.push_back(next);

      mesh.indices.push_back(current + 1);
      mesh.indices.push_back(next + 1);
      mesh.indices.push_back(next);
    }
  }

  mesh.bounds = {glm::vec3{-radius} + center, glm::vec3{radius} + center};

  return mesh;
}
}  // namespace fw
