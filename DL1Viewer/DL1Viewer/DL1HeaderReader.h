#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <regex>
#include <set>
#include <array>
#include <filesystem>

struct DL1HeaderInfo {
    uint32_t vtxCount = 0;
    uint32_t subCount = 0;
    std::vector<uint32_t> triCounts;
    std::vector<uint16_t> matIndices;
    size_t headerOffset = 0;
};

class DL1HeaderReader {
public:
    static std::vector<std::string> ExtractMaterialNames(const std::vector<uint8_t>& matData) {
        std::string rawStr(reinterpret_cast<const char*>(matData.data()), matData.size());
        std::regex matRegex(R"([a-zA-Z0-9_./\\-]+\.mat)", std::regex_constants::icase);

        std::vector<std::string> materials;
        std::set<std::string> seen;

        auto begin = std::sregex_iterator(rawStr.begin(), rawStr.end(), matRegex);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::string fullPath = it->str();
            std::string baseName = std::filesystem::path(fullPath).filename().string();
            if (!baseName.empty() && seen.insert(baseName).second) {
                materials.push_back(baseName);
            }
        }
        return materials;
    }

    static bool ReadSubmeshTable(const std::vector<uint8_t>& hdrData, DL1HeaderInfo& outInfo) {
        std::array<uint8_t, 12> marker;
        marker.fill(0xFF);

        // Сканируем весь буфер заголовков без обрезки на 512 КБ
        size_t limit = hdrData.size();
        size_t pos = 0;
        bool foundAny = false;

        while (pos + 28 <= limit) {
            auto it = std::search(hdrData.begin() + pos, hdrData.end(), marker.begin(), marker.end());
            if (it == hdrData.end()) break;

            size_t found = std::distance(hdrData.begin(), it);
            size_t base = found + 12;
            if (base + 16 > limit) break;

            uint32_t vtxCnt = ReadU32(hdrData, base);
            uint32_t subCnt = ReadU32(hdrData, base + 8);

            if (vtxCnt >= 3 && vtxCnt <= 1000000 && subCnt >= 1 && subCnt <= 256) {
                size_t triTableOff = base + 16;
                size_t matTableOff = triTableOff + subCnt * 4;

                if (matTableOff + subCnt * 2 <= limit) {
                    bool valid = true;
                    std::vector<uint32_t> tris(subCnt);
                    for (uint32_t i = 0; i < subCnt; ++i) {
                        tris[i] = ReadU32(hdrData, triTableOff + i * 4);
                        if (tris[i] == 0 || tris[i] > 2000000) valid = false;
                    }

                    // Выбираем таблицу с наибольшим количеством вершин (основной LOD0)
                    if (valid && (!foundAny || vtxCnt > outInfo.vtxCount)) {
                        outInfo.vtxCount = vtxCnt;
                        outInfo.subCount = subCnt;
                        outInfo.triCounts = std::move(tris);
                        outInfo.headerOffset = base;
                        outInfo.matIndices.resize(subCnt);
                        for (uint32_t i = 0; i < subCnt; ++i) {
                            outInfo.matIndices[i] = ReadU16(hdrData, matTableOff + i * 2);
                        }
                        foundAny = true;
                    }
                }
            }
            pos = found + 4;
        }
        return foundAny;
    }

private:
    static uint16_t ReadU16(const std::vector<uint8_t>& d, size_t off) {
        uint16_t v; std::memcpy(&v, d.data() + off, 2); return v;
    }
    static uint32_t ReadU32(const std::vector<uint8_t>& d, size_t off) {
        uint32_t v; std::memcpy(&v, d.data() + off, 4); return v;
    }
};
