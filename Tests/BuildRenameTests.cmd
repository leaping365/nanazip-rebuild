@echo off
setlocal
cd /d "%~dp0.."
if not exist Output\RenameCheck mkdir Output\RenameCheck
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RenameVisualStudio=%%i"
call "%RenameVisualStudio%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNANAZIP_RENAME_TESTING /W4 /WX Tests\RenameTests.cpp NanaZip.UI.Shared\Rename.Model.cpp /FoOutput\RenameCheck\ /FeOutput\RenameCheck\RenameTests.exe /link user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /W4 /WX Tests\RenamePreview.cpp NanaZip.UI.Shared\Rename.Model.cpp NanaZip.UI.Shared\Rename.Dialog.cpp /FoOutput\RenameCheck\ /FeOutput\RenameCheck\RenamePreview.exe /link /subsystem:windows /manifest:embed /manifestinput:Tests\RenamePreview.manifest user32.lib gdi32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /WX Tests\RenameMenuTests.cpp /FoOutput\RenameCheck\ /FeOutput\RenameCheck\RenameMenuTests.exe /link user32.lib shell32.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /WX Tests\ShellDialogTests.cpp /FoOutput\RenameCheck\ /FeOutput\RenameCheck\ShellDialogTests.exe /link user32.lib shell32.lib ole32.lib uuid.lib
exit /b %errorlevel%
