; BPM & Key Detector - Windows installer
;
; Build this with makensis (installed via `sudo apt install nsis` on Linux -
; no Wine needed to run the compiler, only to run the resulting .exe).
;
; BEFORE compiling this script:
;   1. Download "BPM & Key Detector.vst3" from the GitHub Actions build.
;   2. Place it inside this "installer" folder so the path looks like:
;        installer/VST3/BPM & Key Detector.vst3/  (a folder, not a file)
;
; THEN compile it:
;   cd installer
;   makensis installer.nsi
;   -> produces BpmKeyDetectorSetup.exe in this same folder

!define APP_NAME "BPM & Key Detector"
!define VST3_NAME "BPM & Key Detector.vst3"

Name "${APP_NAME}"
OutFile "BpmKeyDetectorSetup.exe"
InstallDir "$COMMONFILES\VST3\${VST3_NAME}"
RequestExecutionLevel admin

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "VST3\${VST3_NAME}\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
    RMDir /r "$INSTDIR"
SectionEnd
