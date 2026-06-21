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

## 3-1.  MMDリアルタイムミラー表示プラグイン (MMD Mirror Preview_MME非対応版)
MikuMikuDance (MMD) x64 の3D描画画面（ビューポート）をリアルタイムにキャプチャし、独立した別ウィンドウにHD解像度（1280x720）で等倍ミラー表示するプラグインです。
NVIDIA GeForce Experience 等の外部オーバーレイ機能や、他のMMD拡張（MMEffectなど）と競合しにくい、堅牢な `IDirect3DDevice9::Present` フックを採用しています。
「MME_not_supported」 のフォルダはMMEに対応していません。MMEを使用する場合は「MME_compatible」のフォルダを使用してください。

##  主な機能・特徴(MME_not_supported)

- **完全独立ウィンドウ表示**: MMDのメイン画面とは別に「1280x720 固定サイズ」のプレビュー画面を表示します。
- **超高画質・低負荷**: GPU内部でのバッファコピー（`StretchRect` + バイリニアフィルタ）を行っているため、画質の劣化を最小限に抑え、CPUに負荷をかけずにヌルヌル動きます。
- **サイズ変更・バグ対策**: サブウィンドウのサイズは完全に固定されており、ドラッグや操作によるアスペクト比の崩れや無限拡大バグが起きません。

## 導入・使用方法(MME_not_supported)

1. 本プロジェクトを Visual Studio でビルドし、生成された `d3d9.dll` を取得、また付属している`d3d9.dll`を使用します。
2. MMDがあるフォルダ `MikuMikuDance.exe` に、ビルドした `d3d9.dll` を配置します。
3. MMDを通常通り起動します。
4. MMDのメニューバーの右端に **「プラグイン」** という項目が追加されます。
5. **「プラグイン」 > 「ミラー画面を表示」** をクリックすると、独立した「MMDミラー表示」ウィンドウが起動します。
6. ウィンドウの「×」ボタンを押すと、安全にプレビューが閉じ（非表示になり）、処理が一時停止します。再度メニューから表示可能です。

----

## 3-2. MMD Mirror Preview (MMDリアルタイムミラー表示プラグイン_MME対応版)MME_compatible

MikuMikuDance (MMD) x64 の3D描画画面（ビューポート）をリアルタイムにキャプチャし、独立した別ウィンドウにHD解像度（1280x720）で等倍ミラー表示するプラグインです。

**MikuMikuEffect (MME) 環境に完全対応**しており、エフェクト適用後の高画質な映像を、ちらつき（フリッカー）なしでプレビュー画面に転送します。NVIDIA GeForce Experience等の外部オーバーレイや録画ソフトとも競合しにくい設計です。

## 主な機能・特徴(MME_compatible)

- **MME (MikuMikuEffect) 完全共存**: 「DLLリレー（プロキシチェーン）方式」を採用。MMEの高負荷なエフェクト環境下でも、エフェクトが全て適用された最終完成画面のみを正確にキャプチャします。
- **完全独立・固定ウィンドウ**: MMDのメイン画面とは別に「1280x720」固定サイズのプレビュー画面を表示。誤操作によるアスペクト比の崩れを防ぎます。
- **黒枠なし・UI非表示のクリーンアウトプット**: MMDのキャンバス全体ではなく「3Dビューポート領域」だけを切り出し、右上のMMEffectの文字やボーン操作枠などのUIを含まない純粋な映像だけを出力します。
- **超高画質・ちらつき防止**: GPU内部でのバッファコピー（`StretchRect`）と `GetBackBuffer` の厳密なタイミング制御により、マルチパスレンダリング時の不自然なストロボ・ちらつきを完全に排除しました。
- **安心の自己修復設計**: MMDの画面サイズ変更やフルスクリーン化、AVI出力の開始などによる「デバイスロスト」発生時、プラグインが自動でリソースを安全に再構築し、MMDのクラッシュを防ぎます。

## 導入・使用方法

### 【重要】MME (MikuMikuEffect) と併用する場合の導入手順
Windowsの仕様上、同じフォルダに `d3d9.dll` は1つしか置けません。MMEと共存させるため、以下の手順でファイル名を変更してください。

1. MMEをダウンロードし、付属している `d3d9.dll` のファイル名を **`d3d9_mme.dll`** に変更、または付属の物に差し替えます。
2. MMDがインストールされているフォルダ（`MikuMikuDance.exe` がある場所）に、以下のファイルが全て揃うように配置します。
   - `d3d9.dll` （本プラグイン）
   - `d3d9_mme.dll` （名前を変更または差し替えしたMME）
   - `MMEffect.dll` （MME付属）
   - `MMHack.dll` （MME付属）
3. MMDを起動します。（本プラグインが自動的に `d3d9_mme.dll` を読み込み、バトンタッチします）

*(※MMEを使用しない場合は、本プラグインの `d3d9.dll` を入れるだけでシステム標準のDirectXが自動的に読み込まれて動作します)*

### 操作方法
1. MMDを起動すると、メニューバーに **「MMMPreview」** という項目が追加されます。
2. **「MMPreview」 > 「Mirror Previewを表示」** をクリックすると、独立したプレビューウィンドウが起動します。
3. ウィンドウの「×」ボタンを押すと、安全にプレビューが閉じ（非表示になり）、GPUのコピー処理が一時停止します。再度メニューから表示可能です。
4. **「バージョン情報」** をクリックすると、プラグインのバージョン詳細が確認できます。

## ログ機能について
プラグインの動作状況を確認するため、MMDフォルダ内に自動的に `mmd_mirror_plugin.log` が生成されます。
MME（d3d9_mme.dll）の読み込み成否や、デバイスリセットの追従履歴が記録されるため、トラブルシューティングに役立ちます。

## 開発者向け情報 / クレジット

### 使用している外部ライブラリ
- **[MinHook](https://github.com/TsudaKageyu/minhook)** - Minimalistic API Hooking Library for x64/x86 (Released under the [MinHook License](https://github.com/TsudaKageyu/minhook/blob/master/LICENSE.txt))

## 開発ツール

- 本プロジェクトの開発には、Google Gemini,Chatgptを活用しています。
- Visual Studio 2026

