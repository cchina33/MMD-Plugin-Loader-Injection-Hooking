#include "pch.h"
#include <windows.h>
#include <process.h>

// メッセージボックスを表示するテスト関数
void OnMp4ExportClicked() {
    MessageBoxW(NULL, L"プラグインが起動しました！（v1.0-2026-06-13）", L"MMDプラグイン通知", MB_OK | MB_ICONINFORMATION);
}

// ウィンドウメッセージを横取りする関数
WNDPROC g_pOldWndProc = NULL;
LRESULT CALLBACK MmdSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_COMMAND) {
        if (LOWORD(wParam) == 9999) {
            OnMp4ExportClicked();
            return 0;
        }
    }
    return CallWindowProc(g_pOldWndProc, hWnd, uMsg, wParam, lParam);
}

// プロセスIDからHWND（ウィンドウハンドル）を特定するための構造体とコールバック
struct EnumData {
    DWORD dwProcessId;
    HWND hWnd;
};

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    EnumData* pData = (EnumData*)lParam;
    DWORD dwProcessId = 0;
    GetWindowThreadProcessId(hWnd, &dwProcessId);

    // 自分のプロセスIDと一致し、かつメインウィンドウ（メニューを持つもの）を探す
    if (dwProcessId == pData->dwProcessId && GetMenu(hWnd) != NULL && IsWindowVisible(hWnd)) {
        pData->hWnd = hWnd;
        return FALSE; // 見つかったので列挙を終了
    }
    return TRUE;
}

// メニューを追加するメイン処理
unsigned __stdcall InitializePlugin(void* pArguments) {
    // MMDが完全に起動してウィンドウが作られるのをしっかり待つ
    Sleep(2000);

    // 自分のプロセスID（MMDのPID）を取得
    DWORD myPid = GetCurrentProcessId();
    EnumData data = { myPid, NULL };

    // 起動中のウィンドウをすべて走査して、自分のHWNDを特定する
    EnumWindows(EnumWindowsProc, (LPARAM)&data);

    HWND hMmdWnd = data.hWnd;

    if (hMmdWnd) {
        HMENU hMainMenu = GetMenu(hMmdWnd);
        if (hMainMenu) {
            HMENU hSubMenu = CreatePopupMenu();
            AppendMenuW(hSubMenu, MF_STRING, 9999, L"メッセージ表示");
            AppendMenuW(hMainMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"プラグインメニュー");
            DrawMenuBar(hMmdWnd);

            // ウィンドウのサブクラス化（メッセージフック）
            g_pOldWndProc = (WNDPROC)SetWindowLongPtrW(hMmdWnd, GWLP_WNDPROC, (LONG_PTR)MmdSubclassProc);
        }
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        _beginthreadex(NULL, 0, InitializePlugin, NULL, 0, NULL);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}