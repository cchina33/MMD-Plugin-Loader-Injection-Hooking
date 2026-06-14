# MMD Plugin Loader Injection Hooking

##  動作環境

- Windows 10 / 11 (64bit)
- MikuMikuDance Ver9.26(x64) 等で動作確認
- DirectX バージョン: 12 

## 1. MMDplugin_development
`MmdMp4Exporter.dll`: `dllmain.cpp`をビルドしたものです。MMDファイルの直下に配置します。  
`dllmain.cpp`: C++のソースファイルです。ビルドすると`MmdMp4Exporter.dll`ができます。  
`run_mmd.ps1`: MMDにInjectionするWindows PowerShellのコマンドです。  メモ帳等でパスを変更して使用してください。  
以下のコードをpowershellで実行します。  
````
powershell -ExecutionPolicy Bypass .\run_mmd.ps1
````

## 2. MMDplugin_d3d9
`dllmain.cpp`:  C++のソースファイルです。ビルドすると`d3d9.dll`ができます。  
`d3d9.dll`:MMDと同じディレクトリに「d3d9.dll」として配置することで、MMD起動時に自動的に読み込まれるプロキシ（ラッパー）DLLです。  

## 3.  MMDリアルタイムミラー表示プラグイン (MMD Mirror Preview)
MikuMikuDance (MMD) x64 の3D描画画面（ビューポート）をリアルタイムにキャプチャし、独立した別ウィンドウにHD解像度（1280x720）で等倍ミラー表示するプラグインです。
NVIDIA GeForce Experience 等の外部オーバーレイ機能や、他のMMD拡張（MMEffectなど）と競合しにくい、堅牢な `IDirect3DDevice9::Present` フックを採用しています。
※ MMEと併用できません。

##  主な機能・特徴

- **完全独立ウィンドウ表示**: MMDのメイン画面とは別に「1280x720 固定サイズ」のプレビュー画面を表示します。
- **超高画質・低負荷**: GPU内部でのバッファコピー（`StretchRect` + バイリニアフィルタ）を行っているため、画質の劣化を最小限に抑え、CPUに負荷をかけずにヌルヌル動きます。
- **サイズ変更・バグ対策**: サブウィンドウのサイズは完全に固定されており、ドラッグや操作によるアスペクト比の崩れや無限拡大バグが起きません。

## 導入・使用方法

1. 本プロジェクトを Visual Studio でビルドし、生成された `d3d9.dll` を取得、また付属している`d3d9.dll`を使用します。
2. MMDがあるフォルダ `MikuMikuDance.exe` に、ビルドした `d3d9.dll` を配置します。
3. MMDを通常通り起動します。
4. MMDのメニューバーの右端に **「プラグイン」** という項目が追加されます。
5. **「プラグイン」 > 「ミラー画面を表示」** をクリックすると、独立した「MMDミラー表示」ウィンドウが起動します。
6. ウィンドウの「×」ボタンを押すと、安全にプレビューが閉じ（非表示になり）、処理が一時停止します。再度メニューから表示可能です。

## ログ機能について

プラグインの動作状況をチェックするため、MMDと同じフォルダ内に自動的に `mmd_mirror_plugin.log` が生成されます。
APIフックの成否や、メニューのクリック状態、デバイスリセットの追従履歴が記録されるため、トラブルシューティングに役立ちます。

##  開発者向け情報 / クレジット

### 使用している外部ライブラリ
- **[MinHook](https://github.com/TsudaKageyu/minhook)** - Minimalistic API Hooking Library for x64/x86 (Released under the [MinHook License](https://github.com/TsudaKageyu/minhook/blob/master/LICENSE.txt))

