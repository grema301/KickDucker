; ============================================================================
;  KickDucker - Windows VST3 installer (Inno Setup 6.3+ or 7)
;
;  Build:   installer\build_installer.bat            (finds Inno Setup for you)
;      or:  ISCC.exe /DBuildDir=<folder containing KickDucker.vst3> KickDucker.iss
;
;  What it does:
;   * installs the plugin bundle to  C:\Program Files\Common Files\VST3\KickDucker.vst3
;     (the standard system-wide VST3 folder every DAW scans)
;   * puts the UNINSTALLER in C:\Program Files\MSG\KickDucker, NOT in the VST3
;     folder, so your plugin folder isn't littered with unins000.exe
;   * registers in "Apps & features" so users can uninstall normally
; ============================================================================

#define MyAppName      "KickDucker"
#define MyAppVersion   "0.3.0"                  ; keep in sync with the Projucer project version
#define MyAppPublisher "MSG"
#define MyAppURL       "https://grema301.github.io"
#define MyBundleName   "KickDucker.vst3"

; Folder that CONTAINS KickDucker.vst3. Override from the command line with /DBuildDir=...
; Default assumes the Projucer Visual Studio 2022 exporter and this script living in <project>\installer\
#ifndef BuildDir
  #define BuildDir "..\Builds\VisualStudio2022\x64\Release\VST3"
#endif

[Setup]
; Never change this GUID between releases - it's how Windows recognises upgrades/uninstalls.
AppId={{802CF2BE-AED8-4599-904A-D793608C46A6}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}

; Uninstaller + install metadata live here; the plugin itself goes to the VST3 folder (see [Files])
DefaultDirName={autopf}\{#MyAppPublisher}\{#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no

; Writing to Program Files\Common Files needs admin rights
PrivilegesRequired=admin
; 64-bit only (x64compatible also covers Windows 11 on Arm running x64 plugins)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

LicenseFile=EULA.txt
OutputDir=output
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} {#MyAppVersion} (VST3)
; Optional: give the installer + uninstaller your own icon (drop an .ico next to this file)
;SetupIconFile=icon.ico
;UninstallDisplayIcon={app}\icon.ico

[Messages]
WelcomeLabel2=This will install [name/ver] (VST3) on your computer.%n%nPlease close your DAW before continuing, then rescan your plugins afterwards.

[Files]
; The whole bundle folder, including Contents\x86_64-win\KickDucker.vst3 (the DLL) and anything else JUCE generated.
Source: "{#BuildDir}\{#MyBundleName}\*"; DestDir: "{commoncf64}\VST3\{#MyBundleName}"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; Belt and braces: remove the bundle folder even if something else dropped extra files in it
Type: filesandordirs; Name: "{commoncf64}\VST3\{#MyBundleName}"
