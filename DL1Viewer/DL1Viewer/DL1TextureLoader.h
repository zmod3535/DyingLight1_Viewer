#pragma once
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

#include <windows.h>
#include <gl/GL.h>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace fs = std::filesystem;

enum class DL1TexFormat {
    BC1_DXT1, // 8 байт на блок 4x4 (0.5 байта/пиксель)
    BC3_DXT5, // 16 байт на блок 4x4 (1 байт/пиксель)
    BC7_DX10  // 16 байт на блок 4x4 (1 байт/пиксель, DX10)
};

class DL1TextureLoader {
public:
    static GLuint LoadTextureFromFile(const std::string& filepath, std::string& outInfo) {
        std::ifstream f(filepath, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return 0;
        size_t sz = static_cast<size_t>(f.tellg());
        if (sz < 64) return 0;

        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(sz);
        f.read(reinterpret_cast<char*>(data.data()), sz);

        int width = 0, height = 0;
        DL1TexFormat fmt = DL1TexFormat::BC7_DX10;
        size_t pixelOffset = 0;

        // 1. Проверяем заголовок DDS (44 44 53 20 7C)
        if (sz >= 128 && data.at(0) == 0x44 && data.at(1) == 0x44 && data.at(2) == 0x53 && data.at(3) == 0x20 && data.at(4) == 0x7C) {
            std::memcpy(&height, data.data() + 12, 4);
            std::memcpy(&width, data.data() + 16, 4);
            uint32_t fourCC = 0;
            std::memcpy(&fourCC, data.data() + 84, 4);

            if (fourCC == 0x30315844 && sz >= 148) { // 'DX10'
                uint32_t dxgiFormat = 0;
                std::memcpy(&dxgiFormat, data.data() + 128, 4);
                pixelOffset = 148;
                // 70..72 = BC1, 76..78 = BC3, 97..99 = BC7
                if (dxgiFormat >= 70 && dxgiFormat <= 72) fmt = DL1TexFormat::BC1_DXT1;
                else if (dxgiFormat >= 76 && dxgiFormat <= 78) fmt = DL1TexFormat::BC3_DXT5;
                else fmt = DL1TexFormat::BC7_DX10;
            }
            else {
                pixelOffset = 128;
                fmt = (fourCC == 0x31545844) ? DL1TexFormat::BC1_DXT1 : DL1TexFormat::BC3_DXT5;
            }
        }
        else {
            // 2. Сырой дамп без заголовка: в DL1 16-байтные блоки — это BC7 (DX10), 8-байтные — BC1
            if (!GuessRawDimensions(sz, width, height, fmt)) {
                outInfo = "Неизвестный размер сырой текстуры";
                return 0;
            }
            size_t baseSize = static_cast<size_t>(width) * height / (fmt == DL1TexFormat::BC1_DXT1 ? 2 : 1);
            // Берем основной кадр (если есть мипмапы в начале файла, сдвигаемся к концу)
            pixelOffset = (sz > baseSize) ? (sz - baseSize) : 0;
        }

        if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return 0;

        std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 255);
        DecodeBlocks(data.data() + pixelOffset, sz - pixelOffset, width, height, fmt, rgba.data());

        GLuint texID = 0;
        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_2D, texID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        const char* fmtStr = (fmt == DL1TexFormat::BC7_DX10) ? " (DX10 / BC7)" :
            (fmt == DL1TexFormat::BC3_DXT5) ? " (DXT5)" : " (DXT1)";
        outInfo = std::to_string(width) + "x" + std::to_string(height) + fmtStr;
        return texID;
    }

    static GLuint FindAndLoadNearModel(const std::string& modelPath, const std::vector<std::string>& matNames, std::string& outTexName) {
        fs::path p(modelPath);
        fs::path dir = p.parent_path();
        if (!fs::exists(dir)) return 0;

        std::string rawStem = p.stem().string();
        size_t partPos = rawStem.find("_part");
        if (partPos != std::string::npos) rawStem = rawStem.substr(0, partPos);
        size_t meshPos = rawStem.find("_mesh");
        if (meshPos != std::string::npos) rawStem = rawStem.substr(0, meshPos);

        std::string cleanStem = rawStem;
        size_t firstUnderscore = cleanStem.find('_');
        if (firstUnderscore != std::string::npos && firstUnderscore > 0) {
            bool allDigits = true;
            for (size_t i = 0; i < firstUnderscore; ++i) {
                if (!std::isdigit(static_cast<unsigned char>(cleanStem.at(i)))) {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits) cleanStem = cleanStem.substr(firstUnderscore + 1);
        }

        std::vector<std::string> keys;
        if (!cleanStem.empty()) keys.push_back(cleanStem);
        if (rawStem != cleanStem && !rawStem.empty()) keys.push_back(rawStem);
        for (const auto& m : matNames) {
            std::string mStem = fs::path(m).stem().string();
            if (!mStem.empty()) keys.push_back(mStem);
        }

        std::string bestCandidate;
        int bestScore = -1;

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            std::string fn = entry.path().filename().string();
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext != ".dds" && ext != ".tex" && ext != ".bin") continue;
            if (fn.find("_part") != std::string::npos && entry.file_size() < 32768) continue;

            std::string fnLow = fn;
            std::transform(fnLow.begin(), fnLow.end(), fnLow.begin(), ::tolower);

            int score = 0;
            for (const auto& k : keys) {
                std::string kLow = k;
                std::transform(kLow.begin(), kLow.end(), kLow.begin(), ::tolower);
                if (fnLow.find(kLow) != std::string::npos) score += 10;
            }
            if (score == 0) continue;

            if (fnLow.find("_dif") != std::string::npos || fnLow.find("_d.") != std::string::npos || fnLow.find("_alb") != std::string::npos) score += 20;
            if (fnLow.find("_nrm") != std::string::npos || fnLow.find("_n.") != std::string::npos || fnLow.find("_spc") != std::string::npos) score -= 15;

            if (score > bestScore) {
                bestScore = score;
                bestCandidate = entry.path().string();
            }
        }

        if (!bestCandidate.empty()) {
            std::string info;
            GLuint tex = LoadTextureFromFile(bestCandidate, info);
            if (tex != 0) {
                outTexName = fs::path(bestCandidate).filename().string() + " [" + info + "]";
                return tex;
            }
        }
        return 0;
    }

private:
    static bool GuessRawDimensions(size_t sz, int& outW, int& outH, DL1TexFormat& outFmt) {
        const std::vector<int> dims = { 4096, 2048, 1024, 512, 256, 128, 64 };

        // 1. Точные квадратные размеры без мипмапов
        for (size_t i = 0; i < dims.size(); ++i) {
            int d = dims.at(i);
            size_t bpp1 = static_cast<size_t>(d) * d;     // BC7 / DXT5
            size_t bpp05 = static_cast<size_t>(d) * d / 2; // BC1 / DXT1
            if (sz == bpp1) { outW = d; outH = d; outFmt = DL1TexFormat::BC7_DX10; return true; }
            if (sz == bpp05) { outW = d; outH = d; outFmt = DL1TexFormat::BC1_DXT1; return true; }
        }

        // 2. Квадратные размеры с мипмапами (~1.333x)
        for (size_t i = 0; i < dims.size(); ++i) {
            int d = dims.at(i);
            size_t bpp1 = static_cast<size_t>(d) * d;
            size_t bpp05 = static_cast<size_t>(d) * d / 2;
            if (sz > bpp1 && sz <= static_cast<size_t>(bpp1 * 1.36)) { outW = d; outH = d; outFmt = DL1TexFormat::BC7_DX10; return true; }
            if (sz > bpp05 && sz <= static_cast<size_t>(bpp05 * 1.36)) { outW = d; outH = d; outFmt = DL1TexFormat::BC1_DXT1; return true; }
        }

        // 3. Прямоугольные (2:1, 1:2)
        for (size_t i = 0; i < dims.size(); ++i) {
            for (size_t j = 0; j < dims.size(); ++j) {
                int w = dims.at(i);
                int h = dims.at(j);
                if (w == h || w / h > 2 || h / w > 2) continue;
                size_t bpp1 = static_cast<size_t>(w) * h;
                size_t bpp05 = static_cast<size_t>(w) * h / 2;
                if (sz >= bpp1 && sz <= static_cast<size_t>(bpp1 * 1.36)) { outW = w; outH = h; outFmt = DL1TexFormat::BC7_DX10; return true; }
                if (sz >= bpp05 && sz <= static_cast<size_t>(bpp05 * 1.36)) { outW = w; outH = h; outFmt = DL1TexFormat::BC1_DXT1; return true; }
            }
        }
        return false;
    }

    static void DecodeBlocks(const uint8_t* src, size_t srcLen, int width, int height, DL1TexFormat fmt, uint8_t* dstRGBA) {
        int bw = (width + 3) / 4;
        int bh = (height + 3) / 4;
        size_t blockBytes = (fmt == DL1TexFormat::BC1_DXT1) ? 8 : 16;
        size_t pos = 0;

        uint8_t blockRGBA[16 * 4]; // 4x4 пикселя RGBA8

        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                if (pos + blockBytes > srcLen) return;
                const uint8_t* blk = src + pos;
                pos += blockBytes;

                if (fmt == DL1TexFormat::BC7_DX10) {
                    bcdec_bc7(blk, blockRGBA, 4 * 4);
                }
                else if (fmt == DL1TexFormat::BC3_DXT5) {
                    bcdec_bc3(blk, blockRGBA, 4 * 4);
                }
                else {
                    bcdec_bc1(blk, blockRGBA, 4 * 4);
                }

                for (int py = 0; py < 4; ++py) {
                    for (int px = 0; px < 4; ++px) {
                        int x = bx * 4 + px;
                        int y = by * 4 + py;
                        if (x >= width || y >= height) continue;
                        size_t srcOff = (static_cast<size_t>(py) * 4 + px) * 4;
                        size_t dstOff = (static_cast<size_t>(y) * width + x) * 4;
                        std::memcpy(dstRGBA + dstOff, blockRGBA + srcOff, 4);
                    }
                }
            }
        }
    }
};