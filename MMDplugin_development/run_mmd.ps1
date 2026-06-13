# ==========================================
# MMDプラグイン開発用 自動起動・注入スクリプト（互換性重視版）
# ==========================================

# 1. 各自の環境に合わせてパスを設定
$MmdPath = "C:\Users\[Username]\Downloads\MikuMikuDance_v932x64\MikuMikuDance_v932x64\MikuMikuDance.exe"
$DllPath = "C:\Users\[Username]\Downloads\MikuMikuDance_v932x64\MikuMikuDance_v932x64\MmdMp4Exporter.dll"

# ------------------------------------------
# 以下、書き換え不要
# ------------------------------------------
Clear-Host
Write-Host "MMDを起動しています..." -ForegroundColor Cyan

# MMDを起動
$MmdProcess = Start-Process $MmdPath -PassThru
Start-Sleep -Seconds 2 # 完全に起動するまで2秒待機

Write-Host "DLLをインジェクション中..." -ForegroundColor Yellow

# Windows API を動的に定義（[uint] をすべて [UInt32] に統一）
$Win32Methods = @'
[DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(UInt32 a, bool b, int c);
[DllImport("kernel32.dll")] public static extern IntPtr VirtualAllocEx(IntPtr a, IntPtr b, UInt32 c, UInt32 d, UInt32 e);
[DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(IntPtr a, IntPtr b, byte[] c, UInt32 d, IntPtr e);
[DllImport("kernel32.dll")] public static extern IntPtr GetProcAddress(IntPtr a, string b);
[DllImport("kernel32.dll")] public static extern IntPtr GetModuleHandle(string a);
[DllImport("kernel32.dll")] public static extern IntPtr CreateRemoteThread(IntPtr a, IntPtr b, UInt32 c, IntPtr d, IntPtr e, UInt32 f, IntPtr g);
'@

$MmdUtil = Add-Type -MemberDefinition $Win32Methods -Name "MmdUtil" -PassThru

# インジェクション処理を実行
try {
    # MMDのプロセスを開く (0x001F0FFF = PROCESS_ALL_ACCESS)
    $hProcess = $MmdUtil::OpenProcess(0x001F0FFF, $false, $MmdProcess.Id)
    if ($hProcess -eq [IntPtr]::Zero) { throw "MMDのプロセスを開けませんでした。" }

    # DLLのパス文字列（Unicode）をバイト配列に変換
    $DllPathBytes = [System.Text.Encoding]::Unicode.GetBytes($DllPath + "`0")

    # MMDのメモリ内に領域を確保 (0x3000 = MEM_COMMIT|RESERVE, 0x04 = PAGE_READWRITE)
    $pMemory = $MmdUtil::VirtualAllocEx($hProcess, [IntPtr]::Zero, [UInt32]$DllPathBytes.Length, 0x3000, 0x04)
    if ($pMemory -eq [IntPtr]::Zero) { throw "MMD内のメモリ確保に失敗しました。" }

    # 確保したメモリにDLLのパスを書き込み
    $pSuccess = $MmdUtil::WriteProcessMemory($hProcess, $pMemory, $DllPathBytes, [UInt32]$DllPathBytes.Length, [IntPtr]::Zero)
    if (-not $pSuccess) { throw "MMDへのメモリ書き込みに失敗しました。" }

    # LoadLibraryWの関数アドレスを取得
    $hKernel32 = $MmdUtil::GetModuleHandle("kernel32.dll")
    $pLoadLibrary = $MmdUtil::GetProcAddress($hKernel32, "LoadLibraryW")

    # MMD側にスレッドを作ってLoadLibraryWを実行させる
    $hThread = $MmdUtil::CreateRemoteThread($hProcess, [IntPtr]::Zero, 0, $pLoadLibrary, $pMemory, 0, [IntPtr]::Zero)

    if ($hThread -ne [IntPtr]::Zero) {
        Write-Host "インジェクションに成功しました！" -ForegroundColor Green
        Write-Host "MMDのメニューバーの右端に『プラグインメニュー』が追加されているか確認してください。" -ForegroundColor Green
    } else {
        throw "リモートスレッドの作成に失敗しました。"
    }
}
catch {
    Write-Host "エラーが発生しました: $_" -ForegroundColor Red
}

Start-Sleep -Seconds 3