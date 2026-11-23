#pragma once
#include "DL1Types.h"
#include "DL1HeaderReader.h"
#include <cstring>
#include <algorithm>
#include <array>

class DL1FaceReader {
public:
    static bool ReadFaces(const std::vector<uint8_t>& ibData, size_t startPos,
        uint32_t vtxCnt, const DL1HeaderInfo& hdr,
        const std::vector<std::string>& matNames,
        const std::string& meshName,
        std::vector<Submesh>& outSubmeshes) {
        size_t ibOff = 0;
        std::vector<uint32_t> activeTris;

        if (!LocateIndexBuffer(ibData, startPos, vtxCnt, hdr.triCounts, ibOff, activeTris)) {
            return false;
        }

        size_t curTri = 0;
        size_t maxPossibleTris = (ibData.size() - ibOff) / 6;
        uint32_t peakVtxSeen = 0;

        for (size_t subI = 0; subI < activeTris.size(); ++subI) {
            Submesh sm;
            int mSlot = (subI < hdr.matIndices.size()) ? hdr.matIndices.at(subI) : 0;
            sm.materialIndex = mSlot;
            sm.name = (mSlot < static_cast<int>(matNames.size()))
                ? (meshName + "_sub" + std::to_string(subI) + "_" + matNames.at(mSlot))
                : (meshName + "_sub" + std::to_string(subI));

            uint32_t tc = activeTris.at(subI);
            uint32_t safeTc = static_cast<uint32_t>(std::min<size_t>(tc, maxPossibleTris - curTri));

            for (uint32_t t = 0; t < safeTc; ++t) {
                size_t off = ibOff + (curTri + t) * 6;
                uint16_t i0 = ReadU16(ibData, off);
                uint16_t i1 = ReadU16(ibData, off + 2);
                uint16_t i2 = ReadU16(ibData, off + 4);

                uint16_t hi = std::max({ i0, i1, i2 });
                if (hi >= vtxCnt) break;
                if (peakVtxSeen > 500 && hi < 32 && t > 0) break;

                if (i0 != i1 && i1 != i2 && i0 != i2) {
                    sm.faces.push_back({ i0, i1, i2 });
                    if (hi > peakVtxSeen) peakVtxSeen = hi;
                }
            }
            curTri += tc;
            outSubmeshes.push_back(std::move(sm));
        }
        return true;
    }

private:
    static bool LocateIndexBuffer(const std::vector<uint8_t>& ib, size_t startPos, uint32_t vtxCnt,
        const std::vector<uint32_t>& triCounts,
        size_t& outIbOff, std::vector<uint32_t>& outActiveTris) {
        uint32_t rawSum = 0;
        for (uint32_t tc : triCounts) rawSum += tc;

        std::vector<uint32_t> altTris;
        bool hasAlt = (rawSum % 3 == 0);
        for (uint32_t tc : triCounts) if (tc % 3 != 0) hasAlt = false;
        if (hasAlt) {
            for (uint32_t tc : triCounts) altTris.push_back(tc / 3);
        }

        std::vector<uint32_t> targetSums = { rawSum };
        uint32_t altSum = 0;
        if (hasAlt) {
            for (uint32_t tc : altTris) altSum += tc;
            if (altSum != rawSum) targetSums.push_back(altSum);
        }
        std::sort(targetSums.rbegin(), targetSums.rend());

        size_t dataLen = ib.size();

        // 1. Поиск по размеру индексного буфера (t_cnt * 6)
        for (uint32_t tCnt : targetSums) {
            uint32_t targetBytes = tCnt * 6;
            std::array<uint8_t, 4> needle;
            std::memcpy(needle.data(), &targetBytes, 4);

            size_t sPos = startPos;
            while (sPos + 4 <= dataLen) {
                auto it = std::search(ib.begin() + sPos, ib.end(), needle.begin(), needle.end());
                if (it == ib.end()) break;
                size_t idx = std::distance(ib.begin(), it);

                for (size_t delta = 4; delta < 160; ++delta) {
                    size_t cand = idx + delta;
                    if (cand + targetBytes <= dataLen) {
                        bool ok = true;
                        uint32_t step = std::max<uint32_t>(1, tCnt / 40);
                        for (uint32_t t = 0; t < tCnt; t += step) {
                            uint16_t i0 = ReadU16(ib, cand + t * 6);
                            uint16_t i1 = ReadU16(ib, cand + t * 6 + 2);
                            uint16_t i2 = ReadU16(ib, cand + t * 6 + 4);
                            if (std::max({ i0, i1, i2 }) >= vtxCnt) { ok = false; break; }
                        }
                        if (ok) {
                            outIbOff = cand;
                            outActiveTris = (hasAlt && tCnt == altSum) ? altTris : triCounts;
                            return true;
                        }
                    }
                }
                sPos = idx + 4;
            }
        }

        // 2. Fallback-сканер по длиннейшей непрерывной цепочке валидных треугольников (как в Python)
        if (dataLen < 6) return false;
        size_t pos = startPos;
        size_t limit = dataLen - 6;
        size_t bestIbOff = std::string::npos;
        uint32_t bestMaxTris = 0;

        while (pos <= limit) {
            uint16_t a = ReadU16(ib, pos);
            uint16_t b = ReadU16(ib, pos + 2);
            uint16_t c = ReadU16(ib, pos + 4);
            if (a != b && b != c && a != c && std::max({ a, b, c }) < vtxCnt) {
                size_t cur = pos;
                while (cur <= limit) {
                    uint16_t i0 = ReadU16(ib, cur);
                    uint16_t i1 = ReadU16(ib, cur + 2);
                    uint16_t i2 = ReadU16(ib, cur + 4);
                    if (std::max({ i0, i1, i2 }) >= vtxCnt) break;
                    cur += 6;
                }
                uint32_t tc = static_cast<uint32_t>((cur - pos) / 6);
                if (std::find(targetSums.begin(), targetSums.end(), tc) != targetSums.end()) {
                    outIbOff = pos;
                    outActiveTris = (hasAlt && tc == altSum) ? altTris : triCounts;
                    return true;
                }
                if (tc > bestMaxTris && tc >= 1) {
                    bestMaxTris = tc;
                    bestIbOff = pos;
                    pos = cur;
                    continue;
                }
            }
            pos += 1;
        }

        if (bestIbOff == std::string::npos) return false;

        const auto& chosenList = (hasAlt && bestMaxTris == altSum) ? altTris : triCounts;
        outActiveTris.clear();
        uint32_t acc = 0;
        for (uint32_t tc : chosenList) {
            if (acc + tc <= bestMaxTris) {
                outActiveTris.push_back(tc);
                acc += tc;
            }
            else {
                break;
            }
        }
        if (outActiveTris.empty()) outActiveTris.push_back(bestMaxTris);
        else if (acc < bestMaxTris) outActiveTris.push_back(bestMaxTris - acc);

        outIbOff = bestIbOff;
        return true;
    }

    static uint16_t ReadU16(const std::vector<uint8_t>& d, size_t off) {
        uint16_t v; std::memcpy(&v, d.data() + off, 2); return v;
    }
};
