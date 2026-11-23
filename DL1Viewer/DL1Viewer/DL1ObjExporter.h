#pragma once
#include "DL1Types.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>

namespace fs = std::filesystem;

class DL1ObjExporter {
public:
    static bool Export(const DL1Model& model, const std::string& objPath, bool exportCollision) {
        if (model.vertices.empty()) return false;

        fs::path p(objPath);
        fs::path mtlPath = p;
        mtlPath.replace_extension(".mtl");

        // 1. Записываем файл материалов (.mtl)
        std::ofstream mtl(mtlPath);
        if (mtl.is_open()) {
            mtl << "# Dying Light 1 Materials\n\n";
            size_t matCount = std::max<size_t>(model.materials.size(), 1);
            for (size_t i = 0; i < matCount; ++i) {
                std::string mName = (i < model.materials.size()) ? model.materials.at(i) : ("mat_" + std::to_string(i));
                mtl << "newmtl " << mName << "\n";
                mtl << "Kd 0.8 0.8 0.8\n";
                mtl << "Ks 0.1 0.1 0.1\n";
                mtl << "Ns 32.0\n\n";
            }
        }

        // 2. Записываем геометрию (.obj)
        std::ofstream obj(objPath);
        if (!obj.is_open()) return false;

        obj << "# Exported by DL1 Viewer\n";
        obj << "mtllib " << mtlPath.filename().string() << "\n";
        obj << "o " << model.name << "\n\n";

        // Вершины
        for (const auto& v : model.vertices) {
            obj << "v " << v.x << " " << v.y << " " << v.z << "\n";
        }
        obj << "\n";

        // UV-координаты
        bool hasUV = (model.uvs.size() == model.vertices.size());
        if (hasUV) {
            for (const auto& uv : model.uvs) {
                obj << "vt " << uv.u << " " << uv.v << "\n";
            }
            obj << "\n";
        }

        // Сабмеши и полигоны
        for (size_t s = 0; s < model.submeshes.size(); ++s) {
            const auto& sm = model.submeshes.at(s);
            if (sm.faces.empty()) continue;

            std::string mName = (sm.materialIndex >= 0 && sm.materialIndex < static_cast<int>(model.materials.size()))
                ? model.materials.at(sm.materialIndex)
                : ("mat_" + std::to_string(sm.materialIndex));

            obj << "g " << sm.name << "\n";
            obj << "usemtl " << mName << "\n";

            for (const auto& f : sm.faces) {
                uint32_t a = f.i0 + 1;
                uint32_t b = f.i1 + 1;
                uint32_t c = f.i2 + 1;
                if (hasUV) {
                    obj << "f " << a << "/" << a << " " << b << "/" << b << " " << c << "/" << c << "\n";
                }
                else {
                    obj << "f " << a << " " << b << " " << c << "\n";
                }
            }
            obj << "\n";
        }

        // 3. Опциональный экспорт коллизии отдельным файлом *_collision.obj
        if (exportCollision && model.collision.IsValid()) {
            fs::path colPath = p.parent_path() / (p.stem().string() + "_collision.obj");
            std::ofstream colObj(colPath);
            if (colObj.is_open()) {
                colObj << "# Collision Mesh for " << model.name << "\n";
                colObj << "o " << model.name << "_collision\n\n";
                for (const auto& v : model.collision.vertices) {
                    colObj << "v " << v.x << " " << v.y << " " << v.z << "\n";
                }
                colObj << "\n";
                for (const auto& f : model.collision.faces) {
                    colObj << "f " << (f.i0 + 1) << " " << (f.i1 + 1) << " " << (f.i2 + 1) << "\n";
                }
            }
        }

        return true;
    }
};
