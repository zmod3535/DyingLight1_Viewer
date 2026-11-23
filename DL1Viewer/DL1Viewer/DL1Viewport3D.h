#pragma once
#include <windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <cmath>
#include <algorithm>
#include "DL1Types.h"

class DL1Viewport3D {
public:
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;

    const DL1Model* model = nullptr;
    bool wireframe = false;
    bool showCollision = false;
    GLuint textureID = 0;
    bool showTexture = true;

    // Параметры орбитальной камеры (как в OpenIV)
    float yaw = 45.0f;
    float pitch = 25.0f;
    float distance = 3.0f;
    Vec3 target = { 0.0f, 0.0f, 0.0f };

    bool isRotating = false;
    bool isPanning = false;
    POINT lastMouse = { 0, 0 };

    bool Create(HWND parent, int x, int y, int w, int h) {
        if (w < 10) w = 640;
        if (h < 10) h = 420;

        WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "OpenIVStyleGLViewport";
        RegisterClassExA(&wc);

        hwnd = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "OpenIVStyleGLViewport",
            "",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            x, y, w, h,
            parent, nullptr, GetModuleHandle(nullptr), this
        );

        if (!hwnd) return false;
        return InitGL();
    }

    void SetModel(const DL1Model* newModel) {
        model = newModel;
        FitCamera();
        Render();
    }

    void FitCamera() {
        if (!model || model->vertices.empty()) return;
        Vec3 minV = model->vertices.front();
        Vec3 maxV = model->vertices.front();
        for (const auto& v : model->vertices) {
            float gx = v.x;
            float gy = v.y;
            float gz = v.z;
            minV.x = std::min(minV.x, gx); maxV.x = std::max(maxV.x, gx);
            minV.y = std::min(minV.y, gy); maxV.y = std::max(maxV.y, gy);
            minV.z = std::min(minV.z, gz); maxV.z = std::max(maxV.z, gz);
        }

        target = {
            (minV.x + maxV.x) * 0.5f,
            (minV.y + maxV.y) * 0.5f,
            (minV.z + maxV.z) * 0.5f
        };
        float dx = maxV.x - minV.x;
        float dy = maxV.y - minV.y;
        float dz = maxV.z - minV.z;
        float diag = std::sqrt(dx * dx + dy * dy + dz * dz);
        distance = (diag > 0.001f) ? (diag * 1.4f) : 2.5f;
    }

    void Render() {
        if (!hglrc || !hdc || !hwnd) return;
        wglMakeCurrent(hdc, hglrc);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 1;

        glViewport(0, 0, w, h);
        // Темно-сине-серый фон в стиле OpenIV
        glClearColor(0.16f, 0.18f, 0.22f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(45.0, (double)w / (double)h, 0.01, 2000.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        float rYaw = yaw * 3.14159265f / 180.0f;
        float rPitch = pitch * 3.14159265f / 180.0f;
        float camX = target.x + distance * std::cos(rPitch) * std::sin(rYaw);
        float camY = target.y + distance * std::sin(rPitch);
        float camZ = target.z + distance * std::cos(rPitch) * std::cos(rYaw);

        gluLookAt(camX, camY, camZ, target.x, target.y, target.z, 0.0, 1.0, 0.0);

        DrawGridAndAxes();

        if (model && !model->vertices.empty()) {
            glEnable(GL_LIGHTING);
            glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

            bool hasUV = (model->uvs.size() == model->vertices.size());
            if (showTexture && textureID != 0 && hasUV && !wireframe) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, textureID);
                glColor3f(1.0f, 1.0f, 1.0f);
            }
            else {
                glDisable(GL_TEXTURE_2D);
                glColor3f(0.78f, 0.80f, 0.84f);
            }

            glBegin(GL_TRIANGLES);
            for (const auto& sm : model->submeshes) {
                for (const auto& f : sm.faces) {
                    if (f.i0 >= model->vertices.size() || f.i1 >= model->vertices.size() || f.i2 >= model->vertices.size()) continue;
                    const Vec3& v0 = model->vertices[f.i0];
                    const Vec3& v1 = model->vertices[f.i1];
                    const Vec3& v2 = model->vertices[f.i2];

                    float ux = v1.x - v0.x, uy = v1.y - v0.y, uz = v1.z - v0.z;
                    float vx = v2.x - v0.x, vy = v2.y - v0.y, vz = v2.z - v0.z;
                    glNormal3f(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx);

                    if (hasUV) glTexCoord2f(model->uvs.at(f.i0).u, 1.0f - model->uvs.at(f.i0).v);
                    glVertex3f(v0.x, v0.y, v0.z);

                    if (hasUV) glTexCoord2f(model->uvs.at(f.i1).u, 1.0f - model->uvs.at(f.i1).v);
                    glVertex3f(v1.x, v1.y, v1.z);

                    if (hasUV) glTexCoord2f(model->uvs.at(f.i2).u, 1.0f - model->uvs.at(f.i2).v);
                    glVertex3f(v2.x, v2.y, v2.z);

                }
            }
            glEnd();
            glDisable(GL_TEXTURE_2D);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        SwapBuffers(hdc);
    }

    void Destroy() {
        if (hglrc) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc);
            hglrc = nullptr;
        }
        if (hdc && hwnd) {
            ReleaseDC(hwnd, hdc);
            hdc = nullptr;
        }
    }

private:
    bool InitGL() {
        hdc = GetDC(hwnd);
        if (!hdc) return false;

        PIXELFORMATDESCRIPTOR pfd = { sizeof(PIXELFORMATDESCRIPTOR), 1,
            PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
            PFD_TYPE_RGBA, 32, 0,0,0,0,0,0, 0,0,0,0,0,0,0, 24, 8, 0, PFD_MAIN_PLANE, 0,0,0,0 };
        int pf = ChoosePixelFormat(hdc, &pfd);
        if (!pf || !SetPixelFormat(hdc, pf, &pfd)) return false;

        hglrc = wglCreateContext(hdc);
        if (!hglrc || !wglMakeCurrent(hdc, hglrc)) return false;

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);
        glEnable(GL_NORMALIZE);

        float lightPos[4] = { 1.5f, 3.0f, 2.0f, 0.0f };
        float ambient[4] = { 0.30f, 0.32f, 0.36f, 1.0f };
        float diffuse[4] = { 0.85f, 0.85f, 0.85f, 1.0f };
        glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
        glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
        return true;
    }

    void DrawGridAndAxes() {
        glDisable(GL_LIGHTING);
        glBegin(GL_LINES);
        // Сетка
        glColor3f(0.26f, 0.29f, 0.34f);
        for (int i = -10; i <= 10; ++i) {
            if (i == 0) continue;
            glVertex3f((float)i, 0.0f, -10.0f); glVertex3f((float)i, 0.0f, 10.0f);
            glVertex3f(-10.0f, 0.0f, (float)i); glVertex3f(10.0f, 0.0f, (float)i);
        }
        // Оси координат в центре (как в OpenIV: X-красный, Y-зеленый, Z-синий)
        glColor3f(0.85f, 0.2f, 0.2f); glVertex3f(-10.0f, 0.0f, 0.0f); glVertex3f(10.0f, 0.0f, 0.0f);
        glColor3f(0.2f, 0.85f, 0.2f); glVertex3f(0.0f, 0.0f, 0.0f);   glVertex3f(0.0f, 2.0f, 0.0f);
        glColor3f(0.2f, 0.4f, 0.9f);  glVertex3f(0.0f, 0.0f, -10.0f); glVertex3f(0.0f, 0.0f, 10.0f);
        glEnd();
    }

    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        DL1Viewport3D* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
            self = reinterpret_cast<DL1Viewport3D*>(cs->lpCreateParams);
            SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else {
            self = reinterpret_cast<DL1Viewport3D*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
        }

        if (self) {
            switch (msg) {
            case WM_LBUTTONDOWN:
                self->isRotating = true;
                GetCursorPos(&self->lastMouse);
                SetCapture(hWnd);
                return 0;
            case WM_LBUTTONUP:
                self->isRotating = false;
                ReleaseCapture();
                return 0;
            case WM_RBUTTONDOWN:
                self->isPanning = true;
                GetCursorPos(&self->lastMouse);
                SetCapture(hWnd);
                return 0;
            case WM_RBUTTONUP:
                self->isPanning = false;
                ReleaseCapture();
                return 0;
            case WM_MOUSEMOVE: {
                POINT cur;
                GetCursorPos(&cur);
                float dx = static_cast<float>(cur.x - self->lastMouse.x);
                float dy = static_cast<float>(cur.y - self->lastMouse.y);
                if (self->isRotating) {
                    self->yaw -= dx * 0.5f;
                    self->pitch += dy * 0.5f;
                    self->pitch = std::clamp(self->pitch, -89.0f, 89.0f);
                    self->lastMouse = cur;
                    self->Render();
                }
                else if (self->isPanning) {
                    float panSpeed = self->distance * 0.0015f;
                    float rYaw = self->yaw * 3.14159265f / 180.0f;
                    self->target.x -= (dx * std::cos(rYaw)) * panSpeed;
                    self->target.z += (dx * std::sin(rYaw)) * panSpeed;
                    self->target.y += dy * panSpeed;
                    self->lastMouse = cur;
                    self->Render();
                }
                return 0;
            }
            case WM_MOUSEWHEEL: {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                self->distance *= (delta > 0) ? 0.85f : 1.15f;
                if (self->distance < 0.02f) self->distance = 0.02f;
                self->Render();
                return 0;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hWnd, &ps);
                self->Render();
                EndPaint(hWnd, &ps);
                return 0;
            }
            }
        }
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
};
