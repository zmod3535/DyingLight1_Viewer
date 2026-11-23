#define NOMINMAX
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "DL1TextureLoader.h"
#include <windows.h>
#include <commdlg.h>
#include <string>
#include <cstring>
#include <algorithm>

#include "resource.h"
#include "DL1Types.h"
#include "DL1BundleLoader.h"
#include "DL1HeaderReader.h"
#include "DL1VertexReader.h"
#include "DL1FaceReader.h"
#include "DL1CollisionReader.h"
#include "DL1Viewport3D.h"
#include "DL1ObjExporter.h"

DL1Model g_Model;
DL1Viewport3D g_Viewport;
HWND g_HwndStatus = nullptr;

bool LoadAsset(const std::string& path, std::string& outMsg) {
    DL1RawBundle bundle;
    if (!DL1BundleLoader::Load(path, bundle, outMsg)) return false;

    DL1HeaderInfo hdr;
    if (!DL1HeaderReader::ReadSubmeshTable(bundle.hdr_data, hdr)) {
        outMsg = "Не найдена таблица сабмешей (12x FF)";
        return false;
    }

    DL1Model newModel;
    newModel.name = bundle.name;
    newModel.materials = DL1HeaderReader::ExtractMaterialNames(bundle.mat_data);

    VertexLayout layout;
    if (!DL1VertexReader::DetectAndRead(bundle, hdr.vtxCount, newModel.vertices, newModel.uvs, layout)) {
        outMsg = "Ошибка чтения вершин";
        return false;
    }

    size_t ibStart = bundle.isParts ? 0 : (layout.vtxStartOffset + hdr.vtxCount * layout.stride);
    if (!DL1FaceReader::ReadFaces(bundle.ib_data, ibStart, hdr.vtxCount, hdr, newModel.materials, newModel.name, newModel.submeshes)) {
        outMsg = "Ошибка чтения полигонов";
        return false;
    }

    if (!bundle.col_data.empty()) {
        DL1CollisionReader::ReadCollision(bundle.col_data, newModel.collision);
    }

    g_Model = std::move(newModel);
    g_Viewport.SetModel(&g_Model);

    if (g_Viewport.textureID != 0) {
        glDeleteTextures(1, &g_Viewport.textureID);
        g_Viewport.textureID = 0;
    }

    wglMakeCurrent(g_Viewport.hdc, g_Viewport.hglrc);
    std::string texInfo;
    g_Viewport.textureID = DL1TextureLoader::FindAndLoadNearModel(path, g_Model.materials, texInfo);
    g_Viewport.Render();


    size_t totalTris = 0;
    for (const auto& sm : g_Model.submeshes) totalTris += sm.faces.size();
    outMsg = "Модель: " + g_Model.name + " | Вершин: " + std::to_string(g_Model.vertices.size()) +
        " | Треугольников: " + std::to_string(totalTris);
    return true;
}

void OpenFileDialog(HWND owner) {
    char fileBuf[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn = { sizeof(OPENFILENAMEA) };
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "Dying Light 1 Mesh (*.msh;*.bin)\0*.msh;*.bin\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        std::string status;
        if (LoadAsset(fileBuf, status)) {
            if (g_HwndStatus) SetWindowTextA(g_HwndStatus, status.c_str());
        }
        else {
            if (g_HwndStatus) SetWindowTextA(g_HwndStatus, ("Ошибка: " + status).c_str());
            MessageBoxA(owner, status.c_str(), "Ошибка импорта", MB_ICONERROR);
        }
    }
}

void SaveObjDialog(HWND owner) {
    if (g_Model.vertices.empty()) {
        MessageBoxA(owner, "Сначала откройте модель!", "Экспорт OBJ", MB_ICONWARNING);
        return;
    }

    char fileBuf[MAX_PATH] = { 0 };
    std::string defaultName = g_Model.name + ".obj";
    std::memcpy(fileBuf, defaultName.c_str(), std::min<size_t>(defaultName.size(), MAX_PATH - 1));

    OPENFILENAMEA ofn = { sizeof(OPENFILENAMEA) };
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "Wavefront OBJ (*.obj)\0*.obj\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "obj";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (GetSaveFileNameA(&ofn)) {
        bool withCol = (IsDlgButtonChecked(owner, IDC_CHK_COL) == BST_CHECKED);
        if (DL1ObjExporter::Export(g_Model, fileBuf, withCol)) {
            if (g_HwndStatus) SetWindowTextA(g_HwndStatus, ("Экспортировано: " + std::string(fileBuf)).c_str());
        }
        else {
            MessageBoxA(owner, "Не удалось сохранить OBJ файл.", "Ошибка экспорта", MB_ICONERROR);
        }
    }
}

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLong(hDlg, GWL_STYLE, GetWindowLong(hDlg, GWL_STYLE) | WS_CLIPCHILDREN);

        RECT dlgRc;
        GetClientRect(hDlg, &dlgRc);
        int vpX = 12;
        int vpY = 58;
        int vpW = (dlgRc.right - dlgRc.left) - 24;
        int vpH = (dlgRc.bottom - dlgRc.top) - 105;

        HWND hPlaceholder = GetDlgItem(hDlg, IDC_VIEWPORT);
        if (hPlaceholder) {
            RECT rc;
            GetWindowRect(hPlaceholder, &rc);
            MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&rc, 2);
            if ((rc.right - rc.left) > 50 && (rc.bottom - rc.top) > 50) {
                vpX = rc.left;
                vpY = rc.top;
                vpW = rc.right - rc.left;
                vpH = rc.bottom - rc.top;
            }
            DestroyWindow(hPlaceholder);
        }

        g_HwndStatus = GetDlgItem(hDlg, IDC_STATUS);

        if (!g_Viewport.Create(hDlg, vpX, vpY, vpW, vpH)) {
            MessageBoxA(hDlg, "Не удалось инициализировать OpenGL 3D Viewport!", "Ошибка OpenGL", MB_ICONERROR);
        }
        else {
            g_Viewport.Render();
        }

        CheckDlgButton(hDlg, IDC_CHK_TEX, BST_CHECKED);
        return TRUE;
    }
    case WM_SIZE: {
        if (g_Viewport.hwnd) {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            MoveWindow(g_Viewport.hwnd, 12, 58, w - 24, h - 105, TRUE);
            g_Viewport.Render();
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BUTTON1:
            OpenFileDialog(hDlg);
            return TRUE;
        case IDC_BTN_EXPORT:
            SaveObjDialog(hDlg);
            return TRUE;
        case IDC_CHK_WIRE:
            g_Viewport.wireframe = (IsDlgButtonChecked(hDlg, IDC_CHK_WIRE) == BST_CHECKED);
            g_Viewport.Render();
            return TRUE;
        case IDC_CHK_TEX:
            g_Viewport.showTexture = (IsDlgButtonChecked(hDlg, IDC_CHK_TEX) == BST_CHECKED);
            g_Viewport.Render();
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        g_Viewport.Destroy();
        return TRUE;
    }
    return FALSE;
}


int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    DialogBox(hInst, MAKEINTRESOURCE(IDD_DIALOG1), nullptr, MainDlgProc);
    return 0;
}
