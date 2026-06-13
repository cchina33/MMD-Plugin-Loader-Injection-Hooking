/**
 * @file dllmain.cpp
 * @brief MikuMikuDance (MMD) x64 用 DirectX9 ラッパー＆APIフックプラグイン
 * @details
 * 本ソースコードは、MMDと同じディレクトリに「d3d9.dll」として配置することで、
 * MMD起動時に自動的に読み込まれるプロキシ（ラッパー）DLLです。
 * * システム本来の d3d9.dll へ処理をパススルーしつつ、MinHookを利用して
 * IDirect3DDevice9::EndScene を乗っ取り、独自のUI拡張および描画ループへの介入を行います。
 * @date 2026-06-13
 * @version 1.0.0
 * @license MIT License
 */

#include "pch.h"
#include <windows.h>
#include <process.h>
#include <d3d9.h>
#include <atomic> // スレッド間の安全なデータ共有用

 // Windows API のウィンドウ操作・UI操作関連の標準関数を明示的にリンク
#pragma comment(lib, "user32.lib") 

// MinHookの読み込み（GitHubソース直接組み込み、またはNuGet環境に合わせて選択）
// #include <MinHook.h> // NuGetの場合
#include "MinHook.h"    // GitHubからソースを直接プロジェクトへ含めた場合


// ============================================================================
// 1. D3D9 ラッパー（プロキシ）機能
// ============================================================================
// MMDから呼び出される標準の Direct3DCreate9 関数の型定義
typedef IDirect3D9* (WINAPI* Direct3DCreate9_t)(UINT SDKVersion);
static Direct3DCreate9_t pOrgDirect3DCreate9 = NULL;

/**
 * @brief リンカーディレクティブによるエクスポート名の偽装
 * @details
 * C++の仕様上、標準の 'Direct3DCreate9' と完全に同名の関数を定義すると競合（C2375）します。
 * そのため、内部では 'FakeDirect3DCreate9' として実装し、DLLの外部（MMD）に対しては
 * 'Direct3DCreate9' という正規の名前で見えるようにリンカー経由でマッピング（偽装）します。
 */
#pragma comment(linker, "/export:Direct3DCreate9=FakeDirect3DCreate9")

 /**
  * @brief MMDが起動時に呼び出す、偽装された Direct3DCreate9 エントリーポイント
  * @param[in] SDKVersion MMDが要求するD3DのSDKバージョン (通常は D3D_SDK_VERSION)
  * @return システム本来の IDirect3D9 インターフェースへのポインタ
  */
extern "C" IDirect3D9* WINAPI FakeDirect3DCreate9(UINT SDKVersion) {
    if (!pOrgDirect3DCreate9) {
        // システム標準（C:\Windows\System32）の本物の d3d9.dll を動的にロード
        HMODULE hSysD3D9 = LoadLibraryW(L"C:\\Windows\\System32\\d3d9.dll");
        if (hSysD3D9) {
            // 本物の dll から Direct3DCreate9 関数の配置アドレスを割り出す
            pOrgDirect3DCreate9 = (Direct3DCreate9_t)GetProcAddress(hSysD3D9, "Direct3DCreate9");
        }
    }
    // 本物の関数が存在すれば、MMDにそのまま処理を横流し（パススルー）して結果を返す
    return pOrgDirect3DCreate9 ? pOrgDirect3DCreate9(SDKVersion) : NULL;
}


// ============================================================================
// 2. プラグインコア ＆ APIフック機能
// ============================================================================

// MMD本来のウィンドウプロシージャを退避させるポインタ（サブクラス化の解除・チェーン用）
static WNDPROC g_pOldWndProc = NULL;

// 本物の IDirect3DDevice9::EndScene のアドレスを保持するポインタ
typedef HRESULT(STDMETHODCALLTYPE* EndScene_t)(IDirect3DDevice9* pDevice);
static EndScene_t pOrgEndScene = NULL;

/**
 * @brief キャプチャ（処理）のON/OFF状態を管理するフラグ
 * @note
 * UIスレッド（MmdSubclassProc）と描画スレッド（HookedEndScene）の両方から
 * 同時にアクセスされるため、データ競合（undefined behavior）を防ぐためstd::atomicでカプセル化。
 */
static std::atomic<bool> g_bIsCapturing(false);

/**
 * @brief ウィンドウハンドル (HWND) 特定用の探索用データ構造体
 */
struct EnumData {
    DWORD dwProcessId; ///< ターゲットとなるMMDのプロセスID
    HWND hWnd;         ///< 特定したMMDのメインウィンドウハンドル
};

/**
 * @brief 乗っ取った（フックした）DirectX 9 の EndScene 関数
 * @details MMDが1フレームの描画を終え、画面を更新する直前に毎フレーム1回必ず呼ばれます。
 * @param[in] pDevice MMDが内部で生成・使用しているアクティブなDirect3Dデバイス
 * @return HRESULT ステータスコード
 */
HRESULT STDMETHODCALLTYPE HookedEndScene(IDirect3DDevice9* pDevice) {
    // フラグがTrue（メニューから有効化されている）の間だけ、キャプチャ処理等を行う
    if (g_bIsCapturing.load()) {
        /*
         * 【ここに将来の拡張コードを記述】
         * 例:
         * IDirect3DSurface9* pRenderTarget = nullptr;
         * pDevice->GetRenderTarget(0, &pRenderTarget);
         * // 映像バッファを抽出してmp4エンコーダーへ転送するロジックをここに実装します。
         */
    }
    // フック処理が終わったら、必ずMMD本来のEndSceneを呼び出して描画ラインを継続させる
    return pOrgEndScene(pDevice);
}

/**
 * @brief MMDのウィンドウメッセージを横取りするカスタムウィンドウプロシージャ（サブクラス化）
 * @details MMDのメニューバーに追加したカスタム項目のクリックイベント（WM_COMMAND）をピンポイントで検知します。
 */
LRESULT CALLBACK MmdSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_COMMAND) {
        // メニュー項目追加時に指定したメニューID「9999」と一致するか判定
        if (LOWORD(wParam) == 9999) {
            // アトミックにフラグの真偽値を反転 (トグル操作)
            bool currentStatus = !g_bIsCapturing.load();
            g_bIsCapturing.store(currentStatus);

            // ユーザーへ状態が切り替わったことをダイアログで通知
            MessageBoxW(hWnd,
                currentStatus ? L"プラグインが起動しました！（v1.0-2026-06-13）" : L"また押しちゃったのね！",
                L"MMDプラグイン通知",
                MB_OK | MB_ICONINFORMATION);
            return 0; // メッセージを処理したため、0を返して終了（MMD本来の処理には流さない）
        }
    }
    // 自分が処理するメッセージ以外（マウス移動やキー入力など）は、すべてMMD本来のウィンドウプロシージャへ丸投げする
    return CallWindowProc(g_pOldWndProc, hWnd, uMsg, wParam, lParam);
}

/**
 * @brief 起動中の全ウィンドウから、自身（MMD）のメインウィンドウハンドルを特定するコールバック
 */
BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    EnumData* pData = reinterpret_cast<EnumData*>(lParam);
    DWORD dwProcessId = 0;
    GetWindowThreadProcessId(hWnd, &dwProcessId);

    // プロセスIDが一致し、かつメニューバーを保持しており、可視状態のウィンドウを「MMDのメインウィンドウ」と断定する
    if (dwProcessId == pData->dwProcessId && GetMenu(hWnd) != NULL && IsWindowVisible(hWnd)) {
        pData->hWnd = hWnd;
        return FALSE; // ウィンドウが見つかったため、列挙を終了する
    }
    return TRUE; // 見つからない場合は次のウィンドウのチェックへ進む
}

/**
 * @brief プラグインの初期化を行うバックグラウンドスレッド
 * @details MMD本来のメイン処理（描画スレッド等）をロックして止めないよう、DllMainから別スレッドとして非同期に起動されます。
 */
unsigned __stdcall InitializePlugin(void* pArguments) {
    // MMDが起動し、内部のウィンドウクラスやDirect3Dデバイスの初期化構造が完了するまで安全マージンとして待機
    Sleep(3000);

    // --- メニューの動的追加およびメッセージのサブクラス化 ---
    DWORD myPid = GetCurrentProcessId();
    EnumData data = { myPid, NULL };
    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    HWND hMmdWnd = data.hWnd;

    if (hMmdWnd) {
        HMENU hMainMenu = GetMenu(hMmdWnd);
        if (hMainMenu) {
            // 新しいポップアップメニュー（拡張機能のコンテナ）を生成
            HMENU hSubMenu = CreatePopupMenu();
            // メニュー項目をID 9999 で紐付けて登録
            AppendMenuW(hSubMenu, MF_STRING, 9999, L"ダイアログ表示");
            // MMDのメインメニューの一番右端に、作成したサブメニューを「プラグインメニュー」という名前でドッキング
            AppendMenuW(hMainMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"プラグインメニュー");

            // メニューの変更をOSに通知し、UIを再描画させて反映する
            DrawMenuBar(hMmdWnd);

            // MMDのウィンドウプロシージャを自作の 'MmdSubclassProc' に差し替え、古いプロシージャアドレスを退避
            g_pOldWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hMmdWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MmdSubclassProc)));
        }
    }

    // --- MinHookライブラリを用いた DirectX 9 仮想関数テーブル(VTable)のフック ---
    if (MH_Initialize() == MH_OK) {
        // ダミーのIDirect3D9インスタンスを生成
        IDirect3D9* pD3D = FakeDirect3DCreate9(D3D_SDK_VERSION);

        if (pD3D) {
            D3DPRESENT_PARAMETERS d3dpp = {};
            d3dpp.Windowed = TRUE;
            d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            d3dpp.hDeviceWindow = (hMmdWnd ? hMmdWnd : GetDesktopWindow());

            // VTableのアドレスをハックするためだけに、一瞬だけ使い捨てのダミーデバイスを生成する
            IDirect3DDevice9* pDummyDevice = NULL;
            if (SUCCEEDED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice))) {

                // オブジェクトの先頭ポインタから、仮想関数テーブルのアドレス配列（VTable）の先頭を取得
                void** vtable = *reinterpret_cast<void***>(pDummyDevice);

                // IDirect3DDevice9 のVTableにおいて、42番目のインデックスに配置されているのが 'EndScene'
                void* pEndSceneAddr = vtable[42];

                // MinHookに「EndSceneのメモリ番地」「差し替える自作関数」「本物の関数を退避させるポインタ」を渡してフックを作成
                if (MH_CreateHook(pEndSceneAddr, &HookedEndScene, reinterpret_cast<void**>(&pOrgEndScene)) == MH_OK) {
                    // フックを有効化し、命令の書き換えを実行
                    MH_EnableHook(pEndSceneAddr);
                }
                // ダミーデバイスの解放
                pDummyDevice->Release();
            }
            // ダミー用D3Dインスタンスの解放
            pD3D->Release();
        }
    }
    return 0;
}

/**
 * @brief DLLの標準エントリーポイント
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        // DLLロード時、このDLL内の不要なスレッド通知（DLL_THREAD_ATTACH等）を無効化し、デッドロックの危険性を抑止
        DisableThreadLibraryCalls(hModule);

        // MMDのメインスレッドをブロックして起動処理を殺さないよう、初期化ロジックを別スレッドで安全に並行起動
        _beginthreadex(NULL, 0, InitializePlugin, NULL, 0, NULL);
    }
    return TRUE;
}