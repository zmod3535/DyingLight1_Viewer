#pragma once
#include "DL1Types.h"
#include "DL1BundleLoader.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

struct VertexLayout {
    int stride = 0;
    bool isHalfFloat = true;
    int uvOffset = -1;
    size_t vtxStartOffset = 0;
};

class DL1VertexReader {
public:
    static bool DetectAndRead(const DL1RawBundle& bundle, uint32_t vtxCount,
        std::vector<Vec3>& outVerts, std::vector<Vec2>& outUVs,
        VertexLayout& outLayout) {
        if (bundle.isParts) {
            bool found = false;
            for (size_t pad = 0; pad <= 128; pad += 4) {
                if (DetectFromOffset(bundle.vb_data, pad, vtxCount, true, outLayout)) {
                    outLayout.vtxStartOffset = pad;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        else {
            size_t dk1 = DL1BundleLoader::FindTag(bundle.vb_data, "dkcP", 0);
            size_t cStart = (dk1 != std::string::npos) ? dk1 : 0;
            size_t endSearch = (bundle.vb_data.size() > 32) ? std::min(bundle.vb_data.size() - 32, cStart + 16384) : 0;

            bool found = false;
            for (size_t off = cStart + 16; off < endSearch; off += 4) {
                if (DetectFromOffset(bundle.vb_data, off, vtxCount, false, outLayout)) {
                    outLayout.vtxStartOffset = off;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }

        outVerts.resize(vtxCount);
        outUVs.resize(vtxCount);

        for (uint32_t i = 0; i < vtxCount; ++i) {
            size_t base = outLayout.vtxStartOffset + static_cast<size_t>(i) * outLayout.stride;
            float vx, vy, vz;
            if (outLayout.isHalfFloat) {
                vx = HalfToFloat(ReadU16(bundle.vb_data, base));
                vy = HalfToFloat(ReadU16(bundle.vb_data, base + 2));
                vz = HalfToFloat(ReadU16(bundle.vb_data, base + 4));
            }
            else {
                vx = ReadF32(bundle.vb_data, base);
                vy = ReadF32(bundle.vb_data, base + 4);
                vz = ReadF32(bundle.vb_data, base + 8);
            }

            outVerts.at(i) = { vx, vy, vz };

            if (outLayout.uvOffset != -1) {
                float tu = HalfToFloat(ReadU16(bundle.vb_data, base + outLayout.uvOffset));
                float tv = HalfToFloat(ReadU16(bundle.vb_data, base + outLayout.uvOffset + 2));
                outUVs.at(i) = { tu, 1.0f - tv };
            }
        }
        return true;
    }

    static float HalfToFloat(uint16_t h) {
        uint32_t s = (h >> 15) & 0x0001;
        uint32_t e = (h >> 10) & 0x001f;
        uint32_t m = h & 0x03ff;
        if (e == 0) {
            if (m == 0) return s ? -0.0f : 0.0f;
            while (!(m & 0x0400)) { m <<= 1; e--; }
            e++; m &= ~0x0400;
        }
        else if (e == 31) {
            return m == 0 ? (s ? -INFINITY : INFINITY) : NAN;
        }
        uint32_t res = (s << 31) | ((e + 112) << 23) | (m << 13);
        float f; std::memcpy(&f, &res, 4); return f;
    }

    static bool ValidFloat(float v, float limit = 65504.0f) {
        return !std::isnan(v) && !std::isinf(v) && v >= -limit && v <= limit;
    }

private:
    static bool DetectFromOffset(const std::vector<uint8_t>& vb, size_t off, uint32_t vtxCnt, bool isPartFile, VertexLayout& layout) {
        size_t avail = (vb.size() > off) ? (vb.size() - off) : 0;
        if (avail < static_cast<size_t>(vtxCnt) * 12) return false;

        size_t effLen = avail;
        while (effLen > 0 && vb.at(off + effLen - 1) == 0) effLen--;
        if (effLen == 0) effLen = avail;

        uint32_t step = std::max<uint32_t>(1, vtxCnt / 60);
        uint32_t checked = (vtxCnt + step - 1) / step;
        uint32_t minNz = std::min<uint32_t>(2, checked);

        // 1. ПРИОРИТЕТ: Float32 (сначала точное совпадение размера файла, затем 32, 28, 36, 24, 40, 48)
        std::vector<int> f32Strides = { 32, 28, 36, 24, 40, 48 };
        if (isPartFile && vtxCnt > 0) {
            int exact = static_cast<int>(effLen / vtxCnt);
            if (exact >= 16 && exact <= 64 && (exact % 4 == 0)) {
                f32Strides.erase(std::remove(f32Strides.begin(), f32Strides.end(), exact), f32Strides.end());
                f32Strides.insert(f32Strides.begin(), exact);
            }
        }

        for (int s : f32Strides) {
            if (static_cast<size_t>(vtxCnt) * s > avail) continue;

            bool ok = true;
            uint32_t nz = 0;
            uint32_t ffTailCount = 0;
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;

            for (uint32_t i = 0; i < vtxCnt; i += step) {
                size_t b = off + i * s;
                float x = ReadF32(vb, b);
                float y = ReadF32(vb, b + 4);
                float z = ReadF32(vb, b + 8);

                if (!ValidFloat(x, 20000.0f) || !ValidFloat(y, 20000.0f) || !ValidFloat(z, 20000.0f)) { ok = false; break; }
                if ((std::abs(x) > 0 && std::abs(x) < 1e-6f) ||
                    (std::abs(y) > 0 && std::abs(y) < 1e-6f) ||
                    (std::abs(z) > 0 && std::abs(z) < 1e-6f)) {
                    ok = false; break;
                }

                if (std::abs(x) > 1e-4f || std::abs(y) > 1e-4f || std::abs(z) > 1e-4f) nz++;
                if (s == 32 && vb.at(b + 31) == 0xFF) ffTailCount++;

                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
            }

            float maxDim = std::max({ maxX - minX, maxY - minY, maxZ - minZ });
            if (maxDim < 0.0005f) ok = false;

            if (ok && nz >= minNz) {
                int bestUv = FindBestUVOffset(vb, off, vtxCnt, step, s, 12);
                if (bestUv != -1 || ffTailCount > checked / 2) {
                    layout = { s, false, (bestUv != -1 ? bestUv : 20), off };
                    return true;
                }
            }
        }

        // 2. ВТОРОЙ ЭТАП: Half-Float (6 байт XYZ + Padding -> Stride 20, 16, 24, 28, 32, 12...)
        std::vector<int> hfStrides = { 20, 16, 24, 28, 32, 12, 14, 18, 22 };
        if (isPartFile && vtxCnt > 0) {
            int exact = static_cast<int>(effLen / vtxCnt);
            if (exact >= 8 && exact <= 32 && (exact % 2 == 0)) {
                hfStrides.erase(std::remove(hfStrides.begin(), hfStrides.end(), exact), hfStrides.end());
                hfStrides.insert(hfStrides.begin(), exact);
            }
        }

        for (int s : hfStrides) {
            if (static_cast<size_t>(vtxCnt) * s > avail) continue;

            bool ok = true;
            uint32_t nz = 0;
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;

            for (uint32_t i = 0; i < vtxCnt; i += step) {
                size_t b = off + i * s;
                float x = HalfToFloat(ReadU16(vb, b));
                float y = HalfToFloat(ReadU16(vb, b + 2));
                float z = HalfToFloat(ReadU16(vb, b + 4));

                if (!ValidFloat(x, 20000.0f) || !ValidFloat(y, 20000.0f) || !ValidFloat(z, 20000.0f)) { ok = false; break; }
                if (std::abs(x) > 1e-4f || std::abs(y) > 1e-4f || std::abs(z) > 1e-4f) nz++;

                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
            }

            float maxDim = std::max({ maxX - minX, maxY - minY, maxZ - minZ });
            if (maxDim < 0.0005f) ok = false;

            if (ok && nz >= minNz) {
                int bestUv = FindBestUVOffset(vb, off, vtxCnt, step, s, 6);
                if (bestUv != -1 || s <= 12) {
                    layout = { s, true, bestUv, off };
                    return true;
                }
            }
        }

        return false;
    }

    static int FindBestUVOffset(const std::vector<uint8_t>& vb, size_t off, uint32_t vtxCnt, uint32_t step, int stride, int posBytes) {
        int bestUo = -1, bestScore = -1;
        std::vector<int> offsets;
        if (posBytes == 12 && stride == 32) offsets.push_back(20);
        for (int uo = posBytes; uo + 4 <= stride; uo += 2) {
            if (std::find(offsets.begin(), offsets.end(), uo) == offsets.end()) offsets.push_back(uo);
        }

        for (int uo : offsets) {
            int score = 0;
            bool valid = true;
            for (uint32_t i = 0; i < vtxCnt; i += step) {
                float u = HalfToFloat(ReadU16(vb, off + i * stride + uo));
                float v = HalfToFloat(ReadU16(vb, off + i * stride + uo + 2));
                if (!ValidFloat(u, 500.0f) || !ValidFloat(v, 500.0f)) { valid = false; break; }
                if (u >= -10.0f && u <= 10.0f && v >= -10.0f && v <= 10.0f) score += 2;
            }
            if (valid && score > bestScore) {
                bestScore = score;
                bestUo = uo;
            }
        }
        return bestUo;
    }

    static uint16_t ReadU16(const std::vector<uint8_t>& d, size_t off) { uint16_t v; std::memcpy(&v, d.data() + off, 2); return v; }
    static float ReadF32(const std::vector<uint8_t>& d, size_t off) { float v; std::memcpy(&v, d.data() + off, 4); return v; }
};
