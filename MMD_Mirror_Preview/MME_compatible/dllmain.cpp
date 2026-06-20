/**
 * @file dllmain.cpp
 * @brief MikuMikuDance Ver.9.32 x64 用 リアルタイムミラー表示プラグイン (MME完全対応追加版)
 */

#include "pch.h"
#include <windows.h>
#include <process.h>
#include <d3d9.h>
#include <atomic>
#include <fstream>
#include <mutex>
#include <chrono>
#include <cstdarg>

#pragma comment(lib, "user32.lib") 
#include "MinHook.h" 

 // ============================================================================
 // 0. ログ出力機能
 // ============================================================================
std::mutex g_logMutex;
std::ofstream g_logFile;

void InitLog() {
    g_logFile.open("mmd_mirror_plugin.log", std::ios::out | std::ios::app);
    if (g_logFile.is_open()) {
        g_logFile << "\n=========================================\n";
        g_logFile << "Mirror Plugin (MME Compatible) Loaded\n";
        g_logFile << "=========================================\n";
    }
}

void Log(const char* format, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logFile.is_open()) return;

    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now_c);

    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

    g_logFile << "[" << timeStr << "] " << buffer << std::endl;
    g_logFile.flush();
}

// ============================================================================
// 1. D3D9 ラッパー（プロキシチェーン）機能
// ============================================================================
typedef IDirect3D9* (WINAPI* Direct3DCreate9_t)(UINT SDKVersion);
typedef HRESULT(WINAPI* Direct3DCreate9Ex_t)(UINT SDKVersion, IDirect3D9Ex** ppD3D9Ex);

#pragma comment(linker, "/export:Direct3DCreate9=FakeDirect3DCreate9")
#pragma comment(linker, "/export:Direct3DCreate9Ex=FakeDirect3DCreate9Ex")

HMODULE LoadD3D9Dll() {
    static HMODULE hDll = NULL;
    if (!hDll) {
        hDll = LoadLibraryW(L"d3d9_mme.dll");
        if (hDll) {
            Log("[D3D9] Loaded MME proxy (d3d9_mme.dll). Relay active.");
        }
        else {
            hDll = LoadLibraryW(L"C:\\Windows\\System32\\d3d9.dll");
            Log("[D3D9] Loaded System d3d9.dll (No MME detected).");
        }
    }
    return hDll;
}

extern "C" IDirect3D9* WINAPI FakeDirect3DCreate9(UINT SDKVersion) {
    HMODULE hDll = LoadD3D9Dll();
    if (hDll) {
        Direct3DCreate9_t pFunc = (Direct3DCreate9_t)GetProcAddress(hDll, "Direct3DCreate9");
        if (pFunc) return pFunc(SDKVersion);
    }
    return NULL;
}

extern "C" HRESULT WINAPI FakeDirect3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D9Ex) {
    HMODULE hDll = LoadD3D9Dll();
    if (hDll) {
        Direct3DCreate9Ex_t pFunc = (Direct3DCreate9Ex_t)GetProcAddress(hDll, "Direct3DCreate9Ex");
        if (pFunc) return pFunc(SDKVersion, ppD3D9Ex);
    }
    return E_FAIL;
}

// ============================================================================
// 2. プラグインコア ＆ APIフック機能
// ============================================================================
static WNDPROC g_pOldWndProc = NULL;
static std::atomic<bool> g_bIsCapturing(false);

HWND g_hMirrorWnd = NULL;
IDirect3DSwapChain9* g_pSwapChain = NULL;

typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(STDMETHODCALLTYPE* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

static Present_t pOrgPresent = NULL;
static Reset_t pOrgReset = NULL;

LRESULT CALLBACK MirrorWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_CLOSE) {
        g_bIsCapturing.store(false);
        ShowWindow(hWnd, SW_HIDE);
        Log("[UI] Mirror window closed by user.");
        return 0;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

HRESULT STDMETHODCALLTYPE HookedReset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = NULL;
    }
    if (g_hMirrorWnd) {
        DestroyWindow(g_hMirrorWnd);
        g_hMirrorWnd = NULL;
        Log("[Mirror] Mirror Window destroyed for Device Reset.");
    }
    return pOrgReset(pDevice, pPresentationParameters);
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {

    if (g_bIsCapturing.load()) {

        if (!g_hMirrorWnd) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_CLASSDC;
            wc.lpfnWndProc = MirrorWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.lpszClassName = L"MmdMirrorClass";
            RegisterClassExW(&wc);

            DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
            RECT wr = { 0, 0, 1280, 720 };
            AdjustWindowRect(&wr, dwStyle, FALSE);

            g_hMirrorWnd = CreateWindowW(L"MmdMirrorClass", L"MMD Mirror Preview", dwStyle,
                CW_USEDEFAULT, CW_USEDEFAULT,
                wr.right - wr.left, wr.bottom - wr.top,
                NULL, NULL, wc.hInstance, NULL);
        }

        if (g_hMirrorWnd && !g_pSwapChain) {
            RECT rc;
            GetClientRect(g_hMirrorWnd, &rc);

            D3DPRESENT_PARAMETERS d3dpp = {};
            d3dpp.Windowed = TRUE;
            d3dpp.SwapEffect = D3DSWAPEFFECT_COPY;
            d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
            d3dpp.BackBufferWidth = rc.right - rc.left;
            d3dpp.BackBufferHeight = rc.bottom - rc.top;
            d3dpp.hDeviceWindow = g_hMirrorWnd;
            d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

            if (SUCCEEDED(pDevice->CreateAdditionalSwapChain(&d3dpp, &g_pSwapChain))) {
                Log("[Mirror] SwapChain Created: %dx%d", d3dpp.BackBufferWidth, d3dpp.BackBufferHeight);
            }
        }

        if (g_hMirrorWnd && g_pSwapChain) {
            if (!IsWindowVisible(g_hMirrorWnd)) {
                ShowWindow(g_hMirrorWnd, SW_SHOW);
            }

            IDirect3DSurface9* pMmdBackBuffer = NULL;
            IDirect3DSurface9* pMirrorBackBuffer = NULL;

            if (SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pMmdBackBuffer))) {
                if (SUCCEEDED(g_pSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pMirrorBackBuffer))) {

                    D3DVIEWPORT9 vp;
                    RECT srcRect;
                    RECT* pSrcRect = NULL;

                    if (SUCCEEDED(pDevice->GetViewport(&vp))) {
                        srcRect.left = vp.X;
                        srcRect.top = vp.Y;
                        srcRect.right = vp.X + vp.Width;
                        srcRect.bottom = vp.Y + vp.Height;
                        pSrcRect = &srcRect;
                    }

                    pDevice->StretchRect(pMmdBackBuffer, pSrcRect, pMirrorBackBuffer, NULL, D3DTEXF_LINEAR);
                    pMirrorBackBuffer->Release();

                    g_pSwapChain->Present(NULL, NULL, NULL, NULL, 0);
                }
                pMmdBackBuffer->Release();
            }
        }
    }
    else {
        if (g_hMirrorWnd && IsWindowVisible(g_hMirrorWnd)) {
            ShowWindow(g_hMirrorWnd, SW_HIDE);
        }
    }

    return pOrgPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

// --- 以下、MMDのメニュー・初期化処理 ---

struct EnumData {
    DWORD dwProcessId;
    HWND hWnd;
};

LRESULT CALLBACK MmdSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_COMMAND) {
        WORD menuId = LOWORD(wParam);

        // ミラー画面の表示・非表示の切り替え
        if (menuId == 9999) {
            bool currentStatus = !g_bIsCapturing.load();
            g_bIsCapturing.store(currentStatus);
            Log("[UI] Menu clicked! Mirror state: %s", currentStatus ? "ON" : "OFF");
            return 0;
        }
        //  バージョン情報の表示ダイアログ
        else if (menuId == 9998) {
            Log("[UI] Version info clicked.");
            MessageBoxW(hWnd, L"MMD Mirror Preview ver0.1 by Hina33", L"バージョン情報", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }
    // MMD本体にメッセージを流す（ANSI版で流すことでMMDのタイトル文字化けを防止）
    return CallWindowProcA(g_pOldWndProc, hWnd, uMsg, wParam, lParam);
}

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    EnumData* pData = reinterpret_cast<EnumData*>(lParam);
    DWORD dwProcessId = 0;
    GetWindowThreadProcessId(hWnd, &dwProcessId);

    if (dwProcessId == pData->dwProcessId && GetMenu(hWnd) != NULL && IsWindowVisible(hWnd)) {
        pData->hWnd = hWnd;
        return FALSE;
    }
    return TRUE;
}

IDirect3D9* CreateSystemD3D9() {
    HMODULE hSysD3D9 = LoadLibraryW(L"C:\\Windows\\System32\\d3d9.dll");
    if (hSysD3D9) {
        Direct3DCreate9_t pFunc = (Direct3DCreate9_t)GetProcAddress(hSysD3D9, "Direct3DCreate9");
        if (pFunc) return pFunc(D3D_SDK_VERSION);
    }
    return NULL;
}

unsigned __stdcall InitializePlugin(void* pArguments) {
    Log("[Init] Thread started. Waiting for MMD...");
    Sleep(3000);

    DWORD myPid = GetCurrentProcessId();
    EnumData data = { myPid, NULL };
    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    HWND hMmdWnd = data.hWnd;

    if (hMmdWnd) {
        HMENU hMainMenu = GetMenu(hMmdWnd);
        if (hMainMenu) {
            HMENU hSubMenu = CreatePopupMenu();

            // メニュー項目の構築
            AppendMenuW(hSubMenu, MF_STRING, 9999, L"MMD Mirror Previewを表示");
            AppendMenuW(hSubMenu, MF_SEPARATOR, 0, NULL); // 区切り線
            AppendMenuW(hSubMenu, MF_STRING, 9998, L"バージョン情報"); // バージョン情報追加

            AppendMenuW(hMainMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"MMPreview");
            DrawMenuBar(hMmdWnd);
            Log("[Init] Menu added.");

            g_pOldWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hMmdWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MmdSubclassProc)));
        }
    }

    if (MH_Initialize() == MH_OK) {
        IDirect3D9* pD3D = CreateSystemD3D9();
        if (pD3D) {
            D3DPRESENT_PARAMETERS d3dpp = {};
            d3dpp.Windowed = TRUE;
            d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            d3dpp.hDeviceWindow = hMmdWnd ? hMmdWnd : GetDesktopWindow();

            IDirect3DDevice9* pDummyDevice = NULL;
            if (SUCCEEDED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice))) {

                void** vtable = *reinterpret_cast<void***>(pDummyDevice);

                void* pResetAddr = vtable[16];
                void* pPresentAddr = vtable[17];

                if (MH_CreateHook(pResetAddr, &HookedReset, reinterpret_cast<void**>(&pOrgReset)) == MH_OK) {
                    MH_EnableHook(pResetAddr);
                }
                if (MH_CreateHook(pPresentAddr, &HookedPresent, reinterpret_cast<void**>(&pOrgPresent)) == MH_OK) {
                    MH_EnableHook(pPresentAddr);
                    Log("[MinHook] 2-Point Hooks (Reset, Present) established.");
                }

                pDummyDevice->Release();
            }
            pD3D->Release();
        }
    }
    Log("[Init] Thread finished.");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadD3D9Dll();
        InitLog();
        _beginthreadex(NULL, 0, InitializePlugin, NULL, 0, NULL);
    }
    return TRUE;
}