# MMD Plugin Loader Injection Hooking

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
