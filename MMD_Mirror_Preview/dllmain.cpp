/**
 * @file dllmain.cpp
 * @brief MikuMikuDance (MMD) x64 用 リアルタイムミラー表示プラグイン (1280x720 HD 完全固定・全画面表示版)
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
        g_logFile << "Mirror Plugin Loaded / Log Initialized\n";
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
// 1. D3D9 ラッパー（プロキシ）機能
// ============================================================================
typedef IDirect3D9* (WINAPI* Direct3DCreate9_t)(UINT SDKVersion);
static Direct3DCreate9_t pOrgDirect3DCreate9 = NULL;

#pragma comment(linker, "/export:Direct3DCreate9=FakeDirect3DCreate9")

extern "C" IDirect3D9* WINAPI FakeDirect3DCreate9(UINT SDKVersion) {
    if (!pOrgDirect3DCreate9) {
        HMODULE hSysD3D9 = LoadLibraryW(L"C:\\Windows\\System32\\d3d9.dll");
        if (hSysD3D9) {
            pOrgDirect3DCreate9 = (Direct3DCreate9_t)GetProcAddress(hSysD3D9, "Direct3DCreate9");
        }
    }
    return pOrgDirect3DCreate9 ? pOrgDirect3DCreate9(SDKVersion) : NULL;
}

// ============================================================================
// 2. プラグインコア ＆ APIフック機能
// ============================================================================
static WNDPROC g_pOldWndProc = NULL;
static std::atomic<bool> g_bIsCapturing(false);

// ミラーリング用のグローバル変数
HWND g_hMirrorWnd = NULL;
IDirect3DSwapChain9* g_pSwapChain = NULL;

// フック用ポインタ
typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(STDMETHODCALLTYPE* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static Present_t pOrgPresent = NULL;
static Reset_t pOrgReset = NULL;

/**
 * @brief サブウィンドウのメッセージ処理
 */
LRESULT CALLBACK MirrorWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_CLOSE) {
        g_bIsCapturing.store(false);
        ShowWindow(hWnd, SW_HIDE);
        Log("[UI] Mirror window closed by user.");
        return 0;
    }
    // ★修正1：「M」問題対策。強制的にWide(Unicode)版の標準プロシージャを呼ぶ
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

/**
 * @brief デバイスロスト＆無限拡大バグ対策 (Resetのフック)
 */
HRESULT STDMETHODCALLTYPE HookedReset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = NULL;
    }

    if (g_hMirrorWnd) {
        DestroyWindow(g_hMirrorWnd);
        g_hMirrorWnd = NULL;
        Log("[Mirror] Mirror Window completely destroyed for Device Reset.");
    }

    return pOrgReset(pDevice, pPresentationParameters);
}

/**
 * @brief 画面の転送とHD完全固定 (Presentのフック)
 */
HRESULT STDMETHODCALLTYPE HookedPresent(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {

    if (g_bIsCapturing.load()) {

        // 1. サブウィンドウが存在しなければ、1280x720固定で作成する
        if (!g_hMirrorWnd) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_CLASSDC;
            wc.lpfnWndProc = MirrorWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.lpszClassName = L"MmdMirrorClass";
            RegisterClassExW(&wc);

            // タイトルバーを含めたサイズで作成
            DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

            // HDサイズ(1280x720)を枠として作成
            RECT wr = { 0, 0, 1280, 720 };
            AdjustWindowRect(&wr, dwStyle, FALSE);

            g_hMirrorWnd = CreateWindowW(
                L"MmdMirrorClass",
                L"MMDミラー表示", // ここは正しく表示されるようになります
                dwStyle,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                wr.right - wr.left,
                wr.bottom - wr.top,
                NULL,
                NULL,
                wc.hInstance,
                NULL);
        }

        // 2. ミラー画面用のスワップチェーンを作成
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
                Log("[Mirror] Created SwapChain: %dx%d", d3dpp.BackBufferWidth, d3dpp.BackBufferHeight);
            }
        }

        // 3. 実際の画像転送処理
        if (g_hMirrorWnd && g_pSwapChain) {
            if (!IsWindowVisible(g_hMirrorWnd)) {
                ShowWindow(g_hMirrorWnd, SW_SHOW);
            }

            IDirect3DSurface9* pMmdBackBuffer = NULL;
            IDirect3DSurface9* pMirrorBackBuffer = NULL;

            if (SUCCEEDED(pDevice->GetRenderTarget(0, &pMmdBackBuffer))) {
                if (SUCCEEDED(g_pSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pMirrorBackBuffer))) {

                    // ★修正2：黒枠対策。MMDが3Dを描画している「ビューポート」だけを取得
                    D3DVIEWPORT9 vp;
                    RECT srcRect;
                    RECT* pSrcRect = NULL;

                    if (SUCCEEDED(pDevice->GetViewport(&vp))) {
                        // 3Dが描画されている領域の座標をハサミの枠として設定
                        srcRect.left = vp.X;
                        srcRect.top = vp.Y;
                        srcRect.right = vp.X + vp.Width;
                        srcRect.bottom = vp.Y + vp.Height;
                        pSrcRect = &srcRect;
                    }

                    // 切り取った部分(pSrcRect)だけを、ミラー画面全体に引き伸ばしてコピー
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
    if (uMsg == WM_COMMAND && LOWORD(wParam) == 9999) {
        bool currentStatus = !g_bIsCapturing.load();
        g_bIsCapturing.store(currentStatus);
        Log("[UI] Menu clicked! Mirror state: %s", currentStatus ? "ON" : "OFF");
        return 0;
    }
    return CallWindowProc(g_pOldWndProc, hWnd, uMsg, wParam, lParam);
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
            AppendMenuW(hSubMenu, MF_STRING, 9999, L"ミラー画面を表示");
            AppendMenuW(hMainMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"プラグイン");
            DrawMenuBar(hMmdWnd);
            Log("[Init] Menu added.");
            g_pOldWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hMmdWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MmdSubclassProc)));
        }
    }
    else {
        Log("[ERROR] MMD Window not found.");
        return 0;
    }

    if (MH_Initialize() == MH_OK) {
        IDirect3D9* pD3D = FakeDirect3DCreate9(D3D_SDK_VERSION);
        if (pD3D) {
            D3DPRESENT_PARAMETERS d3dpp = {};
            d3dpp.Windowed = TRUE;
            d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            d3dpp.hDeviceWindow = hMmdWnd;

            IDirect3DDevice9* pDummyDevice = NULL;
            if (SUCCEEDED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice))) {

                void** vtable = *reinterpret_cast<void***>(pDummyDevice);
                void* pResetAddr = vtable[16];   // Index 16: Reset
                void* pPresentAddr = vtable[17]; // Index 17: Present

                // Resetのフック
                if (MH_CreateHook(pResetAddr, &HookedReset, reinterpret_cast<void**>(&pOrgReset)) == MH_OK) {
                    MH_EnableHook(pResetAddr);
                    Log("[MinHook] Hooked Reset.");
                }

                // Presentのフック
                if (MH_CreateHook(pPresentAddr, &HookedPresent, reinterpret_cast<void**>(&pOrgPresent)) == MH_OK) {
                    MH_EnableHook(pPresentAddr);
                    Log("[MinHook] Hooked Present.");
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
        InitLog();
        Log("DllMain: DLL_PROCESS_ATTACH");
        _beginthreadex(NULL, 0, InitializePlugin, NULL, 0, NULL);
    }
    return TRUE;
}