[Setup]
AppName=KickDucker
AppVersion=1.0.0
AppPublisher=MSG Audio
AppPublisherURL=https://grema301.github.io
DefaultDirName={commoncf}\VST3\KickDucker.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=KickDucker-Setup-v1.0.0
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
SetupIconFile=icon.ico
UninstallDisplayIcon={app}\KickDucker.vst3

[Files]
Source: "build\KickDucker.vst3\*"; DestDir: "{commoncf}\VST3\KickDucker.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\KickDucker.vst3"