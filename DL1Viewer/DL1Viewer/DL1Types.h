#pragma once
#include <vector>
#include <string>
#include <cstdint>

struct Vec3 { float x, y, z; };
struct Vec2 { float u, v; };
struct Triangle { uint32_t i0, i1, i2; };

struct Submesh {
    std::string name;
    int materialIndex = 0;
    std::vector<Triangle> faces;
};

struct CollisionMesh {
    std::vector<Vec3> vertices;
    std::vector<Triangle> faces;
    bool IsValid() const { return !vertices.empty() && !faces.empty(); }
};

struct DL1Model {
    std::string name;
    std::vector<Vec3> vertices;
    std::vector<Vec2> uvs;
    std::vector<Submesh> submeshes;
    std::vector<std::string> materials;
    CollisionMesh collision;
};

struct DL1RawBundle {
    std::string name;
    bool isParts = false;
    std::vector<uint8_t> vb_data;
    std::vector<uint8_t> ib_data;
    std::vector<uint8_t> hdr_data;
    std::vector<uint8_t> mat_data;
    std::vector<uint8_t> col_data;
};
