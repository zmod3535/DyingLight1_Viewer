#pragma once
#include "DL1Types.h"
#include <fstream>
#include <filesystem>
#include <regex>
#include <map>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

class DL1BundleLoader {
public:
    static bool Load(const std::string& filepath, DL1RawBundle& outBundle, std::string& outError) {
        fs::path p(filepath);
        std::string fname = p.filename().string();
        std::regex partRegex(R"(^(.+)_part_?(\d+)\.bin$)", std::regex_constants::icase);
        std::smatch match;

        if (std::regex_match(fname, match, partRegex)) {
            outBundle.isParts = true;
            outBundle.name = match.str(1);
            fs::path folder = p.parent_path();

            std::map<int, std::vector<uint8_t>> parts;
            for (const auto& entry : fs::directory_iterator(folder)) {
                if (!entry.is_regular_file()) continue;
                std::string f = entry.path().filename().string();
                std::smatch pm;
                if (std::regex_match(f, pm, partRegex)) {
                    if (_stricmp(pm.str(1).c_str(), outBundle.name.c_str()) == 0) {
                        int pNum = std::stoi(pm.str(2));
                        parts[pNum] = ReadBytes(entry.path().string());
                    }
                }
            }

            if (parts.find(3) == parts.end() || parts.find(4) == parts.end()) {
                outError = "Отсутствуют part_03 (вершины) или part_04 (фейсы)";
                return false;
            }

            std::vector<uint8_t> metaParts;
            for (int k : {0, 1, 2}) {
                if (parts.count(k)) {
                    metaParts.insert(metaParts.end(), parts.at(k).begin(), parts.at(k).end());
                }
            }

            outBundle.vb_data = parts.at(3);
            outBundle.ib_data = parts.at(4);
            outBundle.mat_data = metaParts.empty() ? parts.at(4) : metaParts;
            outBundle.hdr_data = metaParts;
            outBundle.hdr_data.insert(outBundle.hdr_data.end(), parts.at(4).begin(), parts.at(4).end());

            for (int pIdx : {1, 2, 0}) {
                if (parts.count(pIdx)) {
                    size_t cDk = FindTag(parts.at(pIdx), "dkcP", 0);
                    if (cDk != std::string::npos) {
                        outBundle.col_data.assign(parts.at(pIdx).begin() + cDk, parts.at(pIdx).end());
                        break;
                    }
                }
            }
            if (outBundle.col_data.empty()) {
                size_t dk1 = FindTag(parts.at(4), "dkcP", 0);
                size_t dk2 = (dk1 != std::string::npos) ? FindTag(parts.at(4), "dkcP", dk1 + 4) : std::string::npos;
                if (dk2 != std::string::npos) {
                    outBundle.col_data.assign(parts.at(4).begin() + dk2, parts.at(4).end());
                }
            }
            return true;
        }

        outBundle.isParts = false;
        outBundle.name = p.stem().string();
        auto fullData = ReadBytes(filepath);
        if (fullData.empty()) {
            outError = "Файл пуст или не найден";
            return false;
        }

        size_t dk1 = FindTag(fullData, "dkcP", 0);
        size_t dk2 = (dk1 != std::string::npos) ? FindTag(fullData, "dkcP", dk1 + 4) : std::string::npos;

        if (dk2 != std::string::npos) {
            outBundle.col_data.assign(fullData.begin() + dk2, fullData.end());
            fullData.resize(dk2);
        }

        outBundle.vb_data = outBundle.ib_data = outBundle.hdr_data = outBundle.mat_data = std::move(fullData);
        return true;
    }

    static size_t FindTag(const std::vector<uint8_t>& data, const char* tag, size_t start) {
        size_t len = std::strlen(tag);
        if (start + len > data.size()) return std::string::npos;
        auto it = std::search(data.begin() + start, data.end(), tag, tag + len);
        return (it == data.end()) ? std::string::npos : std::distance(data.begin(), it);
    }

private:
    static std::vector<uint8_t> ReadBytes(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return {};
        size_t sz = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> buf(sz);
        f.read(reinterpret_cast<char*>(buf.data()), sz);
        return buf;
    }
};
