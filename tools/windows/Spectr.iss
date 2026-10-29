; Traditional Windows installer for a staged Spectr package.
; Build with:
;   ISCC.exe /DSourceRoot="C:\\path\\stage" /DOutputDir="C:\\path\\out" Spectr.iss
; SourceRoot is the directory containing Standalone, VST3, CLAP, and the
; runtime sidecars produced by package-release.ps1.

#ifndef SourceRoot
  #error SourceRoot must point to an extracted, validated Spectr package
#endif
#ifndef OutputDir
  #define OutputDir "output"
#endif
#ifndef PluginArchitecture
  #define PluginArchitecture "arm64ec-win"
#endif
#ifndef DisplayArchitecture
  #define DisplayArchitecture "ARM64EC"
#endif

#define ProductName "Spectr"
#define ProductVersion "0.1.0"
#define AppExe SourceRoot + "\\Standalone\\Spectr.exe"

[Setup]
AppId={{B7A9A4E5-28AE-4C4A-9C0A-1D2D7C8B6E91}
AppName={#ProductName}
AppVersion={#ProductVersion}
AppPublisher=Generous Corp
DefaultDirName={autopf}\Spectr
DefaultGroupName=Spectr
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Spectr-Windows-{#DisplayArchitecture}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Spectr ({#DisplayArchitecture})

[Files]
Source: "{#SourceRoot}\Standalone\Spectr.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\Standalone\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceRoot}\Standalone\*.dat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceRoot}\Launch-Spectr.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\VST3\Spectr.vst3\*"; DestDir: "{cf}\VST3\Spectr.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\CLAP\*"; DestDir: "{cf}\CLAP"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autodesktop}\Spectr"; Filename: "{app}\Launch-Spectr.cmd"; WorkingDir: "{app}"
Name: "{group}\Spectr"; Filename: "{app}\Launch-Spectr.cmd"; WorkingDir: "{app}"
Name: "{group}\Uninstall Spectr"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{cf}\VST3\Spectr.vst3"
Type: filesandordirs; Name: "{cf}\CLAP\Spectr.clap"
