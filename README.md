# MMD Plugin Loader Injection Hooking

## 1. MMDplugin_development
`MmdMp4Exporter.dll`: `dllmain.cpp`をビルドしたものです。MMDファイルの直下に配置します。  
`dllmain.cpp`: C++のソースファイルです。ビルドすると`MmdMp4Exporter.dll`ができます。  
`run_mmd.ps1`: MMDにInjectionするWindows PowerShellのコマンドです。  メモ帳等でパスを変更して使用してください。  
以下のコードをpowershellで実行します。  
````
powershell -ExecutionPolicy Bypass .\run_mmd.ps1
````
