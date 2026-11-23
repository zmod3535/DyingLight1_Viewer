#pragma once
#include "DL1Types.h"
#include "DL1VertexReader.h"
#include <cstring>
#include <algorithm>
#include <cmath>

class DL1CollisionReader {
public:
    static bool ReadCollision(const std::vector<uint8_t>& colData, CollisionMesh& outCol) {
        if (colData.size() < 64) return false;
        size_t cLen = colData.size();
        const int strides[3] = { 12, 16, 20 };

        for (size_t vOff = 16; vOff < std::min<size_t>(cLen - 36, 512); vOff += 4) {
            for (int sIdx = 0; sIdx < 3; ++sIdx) {
                int stride = strides[sIdx];
                size_t maxV = (cLen - vOff) / stride;
                if (maxV < 4) continue;

                std::vector<Vec3> verts;
                for (size_t i = 0; i < std::min<size_t>(maxV, 4096); ++i) {
                    size_t off = vOff + i * stride;
                    float vx = ReadF32(colData, off);
                    float vy = ReadF32(colData, off + 4);
                    float vz = ReadF32(colData, off + 8);

                    if (!DL1VertexReader::ValidFloat(vx, 5000.0f) ||
                        !DL1VertexReader::ValidFloat(vy, 5000.0f) ||
                        !DL1VertexReader::ValidFloat(vz, 5000.0f)) break;
                    if ((std::abs(vx) > 0 && std::abs(vx) < 1e-7f) ||
                        (std::abs(vy) > 0 && std::abs(vy) < 1e-7f)) break;

                    verts.push_back({ vx, vy, vz });
                }

                size_t vCnt = verts.size();
                if (vCnt < 4) continue;

                size_t ibStart = vOff + vCnt * stride;
                for (size_t p = ibStart; p < std::min<size_t>(cLen - 12, ibStart + 256); p += 2) {
                    std::vector<Triangle> faces;
                    size_t cur = p;
                    while (cur + 6 <= cLen) {
                        uint16_t i0 = ReadU16(colData, cur);
                        uint16_t i1 = ReadU16(colData, cur + 2);
                        uint16_t i2 = ReadU16(colData, cur + 4);
                        if (std::max({ i0, i1, i2 }) >= vCnt || i0 == i1 || i1 == i2 || i0 == i2) break;
                        faces.push_back({ i0, i1, i2 });
                        cur += 6;
                    }
                    if (faces.size() >= 4) {
                        outCol.vertices = std::move(verts);
                        outCol.faces = std::move(faces);
                        return true;
                    }
                }
            }
        }
        return false;
    }

private:
    static uint16_t ReadU16(const std::vector<uint8_t>& d, size_t off) { uint16_t v; std::memcpy(&v, &d[off], 2); return v; }
    static float ReadF32(const std::vector<uint8_t>& d, size_t off) { float v; std::memcpy(&v, &d[off], 4); return v; }
};
