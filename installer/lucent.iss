; Lucent TTS SAPI 5 wrapper - Inno Setup 6 script.
; Built by build_all.bat with ISCC from %LOCALAPPDATA%\Programs\Inno Setup 6.
; Installer logging: Inno writes a full log to %TEMP%\Setup Log <date> #<n>.txt
; (SetupLogging=yes); the wrapper's own logs go to %LOCALAPPDATA%\LucentSAPI\logs.

#define MyAppName "Lucent TTS SAPI 5"
#define MyAppVersion "1.0.1"
#define MyAppPublisher "Lucent TTS SAPI 5 wrapper project"
#define MyAppURL "https://github.com/joshknnd1982/lucent-tts-sapi5"
#define MyAppCopyright "Open source wrapper; see LICENSE. Lucent Technologies text-to-speech engine is the property of its owners."
#define SrcRoot ".."

[Setup]
AppId={{6C2D4B9E-8F1A-4E7C-9B3D-5A7F2C8E1D64}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
AppCopyright={#MyAppCopyright}
DefaultDirName={autopf}\LucentSAPI
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputBaseFilename=LucentSAPI_Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
SetupLogging=yes
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\LucentConfig.exe
WizardStyle=modern
CloseApplications=no
SetupIconFile={#SrcRoot}\src\config.ico

; Identity metadata for the generated Setup.exe.  Without these the file ships with a
; blank FileVersion and no copyright, which is one of the signals Microsoft Defender's
; ML models and SmartScreen weigh when they score an unsigned download.
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}
VersionInfoDescription={#MyAppName} Setup
VersionInfoCopyright={#MyAppCopyright}
VersionInfoOriginalFileName=LucentSAPI_Setup.exe

; Authenticode signing.  build_all.bat passes /DSIGN and /Slucentsign=... when a code
; signing certificate is configured, which signs both Setup.exe and the uninstaller that
; Inno generates at install time.  Unsigned builds simply skip this.
#ifdef SIGN
SignTool=lucentsign
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon for the Lucent TTS Configuration utility"; GroupDescription: "Additional icons:"

[Files]
; 32-bit SAPI engine DLL, registered with the 32-bit regsvr32 view
Source: "{#SrcRoot}\output\LucentSAPI.dll"; DestDir: "{app}"; Flags: ignoreversion regserver 32bit
; 64-bit SAPI engine DLL (only on 64-bit Windows)
Source: "{#SrcRoot}\output\x64\LucentSAPI.dll"; DestDir: "{app}\x64"; Flags: ignoreversion regserver 64bit; Check: Is64BitInstallMode
; configuration utility
Source: "{#SrcRoot}\output\LucentConfig.exe"; DestDir: "{app}"; Flags: ignoreversion
; the Lucent engine, its channel files, per-language data and the intonation parameter
; copies that must sit next to ttsserver.exe
Source: "{#SrcRoot}\bin\engine\*"; DestDir: "{app}\engine"; Flags: ignoreversion recursesubdirs createallsubdirs
; documentation and samples
Source: "{#SrcRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcRoot}\docs\PROTOCOL.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Registry]
; Both registry views so the x86 and x64 DLLs each find the engine.
Root: HKLM32; Subkey: "Software\LucentSAPI"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\LucentSAPI"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey; Check: Is64BitInstallMode

[Icons]
Name: "{group}\Lucent TTS Configuration"; Filename: "{app}\LucentConfig.exe"
Name: "{group}\Lucent TTS log folder"; Filename: "{localappdata}\LucentSAPI\logs"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Lucent TTS Configuration"; Filename: "{app}\LucentConfig.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\LucentConfig.exe"; Description: "Open the Lucent TTS Configuration utility"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\LucentSAPI\channels"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    Log('Lucent TTS SAPI 5: files installed to ' + ExpandConstant('{app}'));
    Log('Lucent TTS SAPI 5: wrapper logs live in ' + ExpandConstant('{localappdata}') + '\LucentSAPI\logs');
  end;
end;

procedure InitializeUninstallProgressForm();
begin
  Log('Lucent TTS SAPI 5: uninstalling');
end;
