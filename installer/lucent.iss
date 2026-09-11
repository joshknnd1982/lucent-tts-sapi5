; Lucent TTS SAPI 5 wrapper - Inno Setup 6 script.
; Built by build_all.bat with ISCC from %LOCALAPPDATA%\Programs\Inno Setup 6.
; Installer logging: Inno writes a full log to %TEMP%\Setup Log <date> #<n>.txt
; (SetupLogging=yes); the wrapper's own logs go to %LOCALAPPDATA%\LucentSAPI\logs.
;
; The wizard lets you pick exactly which of the eight languages and which of the
; sixteen named voices are installed - the components page is an ordinary
; TreeView, so a screen reader reads and toggles it like any other checkbox list.
; Setup records the choice in {app}\voices.ini, which the SAPI 5 DLLs and the
; configuration utility read (src\installed_voices.cpp), so the Windows voice
; list only ever offers voices whose data files are actually on disk.

#define MyAppName "Lucent TTS SAPI 5"
#define MyAppVersion "1.1.1"
#define MyAppPublisher "Lucent TTS SAPI 5 wrapper project"
#define MyAppURL "https://github.com/joshknnd1982/lucent-tts-sapi5"
#define MyAppCopyright "Open source wrapper; see LICENSE. Lucent Technologies text-to-speech engine is the property of its owners."
#define SrcRoot ".."
#define Engine  SrcRoot + "\bin\engine"
#define Langs   Engine + "\data\languages"
#define Chan    Engine + "\data\chfiles"

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
; The version is in the file name so two downloads in one folder stay apart.
OutputBaseFilename=LucentSAPI_Setup_{#MyAppVersion}
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
VersionInfoOriginalFileName=LucentSAPI_Setup_{#MyAppVersion}.exe

; Authenticode signing.  build_all.bat passes /DSIGN and /Slucentsign=... when a code
; signing certificate is configured, which signs both Setup.exe and the uninstaller that
; Inno generates at install time.  Unsigned builds simply skip this.
#ifdef SIGN
SignTool=lucentsign
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full - all 8 languages and all 16 voices"
Name: "english"; Description: "US English only - John and Grace"
Name: "male"; Description: "All languages, male voices only"
Name: "custom"; Description: "Custom - choose languages and voices"; Flags: iscustom

[Components]
Name: "core"; Description: "Program files and shared engine (required)"; Types: full english male custom; Flags: fixed
Name: "core\x64"; Description: "64-bit SAPI 5 interface"; Types: full english male custom; Check: Is64BitInstallMode

; Each language carries its own front end and text data; its voices are children,
; so clearing a language clears its voices with it.
Name: "lang"; Description: "Languages and voices"; Types: full english male custom; Flags: fixed

Name: "lang\engusg"; Description: "US English"; Types: full english male
Name: "lang\engusg\john"; Description: "John (male)"; Types: full english male
Name: "lang\engusg\grace"; Description: "Grace (female)"; Types: full english

Name: "lang\deudes"; Description: "German"; Types: full male
Name: "lang\deudes\rainer"; Description: "Rainer (male)"; Types: full male
Name: "lang\deudes\monika"; Description: "Monika (female)"; Types: full

Name: "lang\frafrs"; Description: "French"; Types: full male
Name: "lang\frafrs\pierre"; Description: "Pierre (male)"; Types: full male
Name: "lang\frafrs\madeleine"; Description: "Madeleine (female)"; Types: full

Name: "lang\fracaq"; Description: "Canadian French"; Types: full male
Name: "lang\fracaq\jacques"; Description: "Jacques (male)"; Types: full male
Name: "lang\fracaq\yvette"; Description: "Yvette (female)"; Types: full

Name: "lang\itaits"; Description: "Italian"; Types: full male
Name: "lang\itaits\carlo"; Description: "Carlo (male)"; Types: full male
Name: "lang\itaits\giulia"; Description: "Giulia (female)"; Types: full

Name: "lang\esless"; Description: "Castilian Spanish (8000 Hz only)"; Types: full male
Name: "lang\esless\pedro"; Description: "Pedro (male)"; Types: full male
Name: "lang\esless\juanita"; Description: "Juanita (female)"; Types: full

Name: "lang\eslmxm"; Description: "Mexican Spanish"; Types: full male
Name: "lang\eslmxm\pablo"; Description: "Pablo (male)"; Types: full male
Name: "lang\eslmxm\carmen"; Description: "Carmen (female)"; Types: full

Name: "lang\chixxm"; Description: "Mandarin Chinese"; Types: full male
Name: "lang\chixxm\ming"; Description: "Ming (male, GB 2312 text)"; Types: full male
Name: "lang\chixxm\mingbig5"; Description: "Ming Big5 (male, Big5 text)"; Types: full male

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon for the Lucent TTS Configuration utility"; GroupDescription: "Additional icons:"

[Files]
; --- program files ---
; Upgrading while a SAPI application is running is the normal case, and any
; application that has merely listed the installed voices still has the DLL
; mapped.  Deleting a mapped image fails with "access denied", which without
; restartreplace aborts and rolls back the entire installation - so an upgrade
; would fail for exactly the users most likely to be running one.  With it, a
; DLL that cannot be replaced now is replaced on the next restart instead; the
; COM registration is only registry entries naming the path, so it stays valid
; across the swap.  CloseApplications is off on purpose: setup must never close
; somebody's screen reader.
;
; 32-bit SAPI engine DLL, registered with the 32-bit regsvr32 view
Source: "{#SrcRoot}\output\LucentSAPI.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 32bit; Components: core
; 64-bit SAPI engine DLL (only on 64-bit Windows)
Source: "{#SrcRoot}\output\x64\LucentSAPI.dll"; DestDir: "{app}\x64"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 64bit; Check: Is64BitInstallMode; Components: core\x64
; configuration utility
Source: "{#SrcRoot}\output\LucentConfig.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete; Components: core

; --- shared engine ---
; ttsserver.exe and the language-independent intonation databank every channel uses.
; One ttsserver.exe runs per SAPI client process, so this one can be mapped too.
Source: "{#Engine}\ttsserver.exe"; DestDir: "{app}\engine"; Flags: ignoreversion restartreplace uninsrestartdelete; Components: core
Source: "{#Langs}\common\*"; DestDir: "{app}\engine\data\languages\common"; Flags: ignoreversion; Components: core

; --- per-language data ---
; x<lang>.inton must sit next to ttsserver.exe: the intonation module keys its databank
; on the bare -parameters name, so the channel files refer to this copy and not to the
; one inside the language folder.  Mandarin ships no intonation parameter file.
;
; Only US English has separate female inventories (bengusg??f.*); in every other
; language the female voice re-parameterises the male inventory, so those files belong
; to the language rather than to one of its voices.
Source: "{#Engine}\xengusg.inton"; DestDir: "{app}\engine"; Flags: ignoreversion; Components: lang\engusg
Source: "{#Langs}\engusg\*"; Excludes: "bengusg*"; DestDir: "{app}\engine\data\languages\engusg"; Flags: ignoreversion; Components: lang\engusg
Source: "{#Langs}\engusg\bengusg??m.*"; DestDir: "{app}\engine\data\languages\engusg"; Flags: ignoreversion; Components: lang\engusg\john
Source: "{#Langs}\engusg\bengusg??f.*"; DestDir: "{app}\engine\data\languages\engusg"; Flags: ignoreversion; Components: lang\engusg\grace

Source: "{#Engine}\xdeudes.inton"; DestDir: "{app}\engine"; Flags: ignoreversion; Components: lang\deudes
Source: "{#Langs}\deudes\*"; DestDir: "{app}\engine\data\languages\deudes"; Flags: ignoreversion; Components: lang\deudes

Source: "{#Engine}\xfrafrs.inton"; DestDir: "{app}\engine"; Flags: ignoreversion; Components: lang\frafrs
Source: "{#Langs}\frafrs\*"; DestDir: "{app}\engine\data\languages\frafrs"; Flags: ignoreversion; Components: lang\frafrs

Source: "{#Engine}\xfracaq.inton"; DestDir: "{app}\engine"; Flags: ignoreversion; Components: lang\fracaq
Source: "{#Langs}\fracaq\*"; DestDir: "{app}\engine\data\languages\fracaq"; Flags: ignoreversion; Components: lang\fracaq

Source: "{#Engine}\xitaits.inton"; DestDir: "{app}\engine"; Flags: ignoreversion; Components: lang\itaits
Source: "{#Langs}\itaits\*"; DestDir: "{app}\engine\data\languages\itaits"; Flags: ignoreversion; Components: lang\itaits

Source: "{#Engine}\xesless.inton"; DestDir: "{app}\engine"; Flags: ignoreversion; Components: lang\esless
Source: "{#Langs}\esless\*"; DestDir: "{app}\engine\data\languages\esless"; Flags: ignoreversion; Components: lang\esless

Source: "{#Engine}\xeslmxm.inton"; DestDir: "{app}\engine"; Flags: ignoreversion; Components: lang\eslmxm
Source: "{#Langs}\eslmxm\*"; DestDir: "{app}\engine\data\languages\eslmxm"; Flags: ignoreversion; Components: lang\eslmxm

Source: "{#Langs}\chixxm\*"; DestDir: "{app}\engine\data\languages\chixxm"; Flags: ignoreversion; Components: lang\chixxm

; --- per-voice channel templates ---
; One template per sample rate and per e-mail preprocessing variant; the wrapper
; rewrites the paths in them and caches the result under
; %LOCALAPPDATA%\LucentSAPI\channels.  "m" is male, "f" female, and "mg" the Mandarin
; voice that expects GB 2312 text rather than Big5.
Source: "{#Chan}\xengusg.??m*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\engusg\john
Source: "{#Chan}\xengusg.??f*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\engusg\grace
Source: "{#Chan}\xdeudes.??m*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\deudes\rainer
Source: "{#Chan}\xdeudes.??f*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\deudes\monika
Source: "{#Chan}\xfrafrs.??m*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\frafrs\pierre
Source: "{#Chan}\xfrafrs.??f*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\frafrs\madeleine
Source: "{#Chan}\xfracaq.??m*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\fracaq\jacques
Source: "{#Chan}\xfracaq.??f*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\fracaq\yvette
Source: "{#Chan}\xitaits.??m*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\itaits\carlo
Source: "{#Chan}\xitaits.??f*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\itaits\giulia
Source: "{#Chan}\xesless.??m*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\esless\pedro
Source: "{#Chan}\xesless.??f*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\esless\juanita
Source: "{#Chan}\xeslmxm.??m*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\eslmxm\pablo
Source: "{#Chan}\xeslmxm.??f*.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\eslmxm\carmen
Source: "{#Chan}\xchixxm.??mg.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\chixxm\ming
Source: "{#Chan}\xchixxm.??m.chn"; DestDir: "{app}\engine\data\chfiles"; Flags: ignoreversion; Components: lang\chixxm\mingbig5

; --- documentation ---
Source: "{#SrcRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion; Components: core
Source: "{#SrcRoot}\docs\PROTOCOL.md"; DestDir: "{app}\docs"; Flags: ignoreversion; Components: core

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
Type: files; Name: "{app}\voices.ini"
Type: filesandordirs; Name: "{app}\engine"
Type: filesandordirs; Name: "{localappdata}\LucentSAPI\channels"

[Code]
const
  LangCount = 8;
  VoiceCount = 16;

var
  // voices.ini key, component name (also the engine's file prefix), display name
  LangKey: array[0..LangCount - 1] of String;
  LangComp: array[0..LangCount - 1] of String;
  LangName: array[0..LangCount - 1] of String;

  // a voice's voices.ini key is "<language key>.<name without spaces>"
  VoiceKey: array[0..VoiceCount - 1] of String;
  VoiceComp: array[0..VoiceCount - 1] of String;
  VoiceLang: array[0..VoiceCount - 1] of Integer;
  // channel-file suffix: the voice owns x<lang>.NN<suffix>[e].chn
  VoiceSuffix: array[0..VoiceCount - 1] of String;

procedure AddLanguage(Index: Integer; Key, Comp, Display: String);
begin
  LangKey[Index] := Key;
  LangComp[Index] := Comp;
  LangName[Index] := Display;
end;

procedure AddVoice(Index, Lang: Integer; VName, Comp, Suffix: String);
begin
  VoiceLang[Index] := Lang;
  VoiceKey[Index] := LangKey[Lang] + '.' + VName;
  VoiceComp[Index] := LangComp[Lang] + '\' + Comp;
  VoiceSuffix[Index] := Suffix;
end;

// Mirrors kLanguages and kSpeakers in src\lucent_settings.cpp.  The voice names
// here have had their spaces removed, which is how src\installed_voices.cpp
// builds the key it looks up.
procedure InitVoiceTables;
begin
  AddLanguage(0, 'EnglishUS',        'engusg', 'US English');
  AddLanguage(1, 'German',           'deudes', 'German');
  AddLanguage(2, 'French',           'frafrs', 'French');
  AddLanguage(3, 'FrenchCanadian',   'fracaq', 'Canadian French');
  AddLanguage(4, 'Italian',          'itaits', 'Italian');
  AddLanguage(5, 'SpanishCastilian', 'esless', 'Castilian Spanish');
  AddLanguage(6, 'SpanishMexican',   'eslmxm', 'Mexican Spanish');
  AddLanguage(7, 'ChineseMandarin',  'chixxm', 'Mandarin Chinese');

  AddVoice(0,  0, 'John',      'john',      'm');
  AddVoice(1,  0, 'Grace',     'grace',     'f');
  AddVoice(2,  1, 'Rainer',    'rainer',    'm');
  AddVoice(3,  1, 'Monika',    'monika',    'f');
  AddVoice(4,  2, 'Pierre',    'pierre',    'm');
  AddVoice(5,  2, 'Madeleine', 'madeleine', 'f');
  AddVoice(6,  3, 'Jacques',   'jacques',   'm');
  AddVoice(7,  3, 'Yvette',    'yvette',    'f');
  AddVoice(8,  4, 'Carlo',     'carlo',     'm');
  AddVoice(9,  4, 'Giulia',    'giulia',    'f');
  AddVoice(10, 5, 'Pedro',     'pedro',     'm');
  AddVoice(11, 5, 'Juanita',   'juanita',   'f');
  AddVoice(12, 6, 'Pablo',     'pablo',     'm');
  AddVoice(13, 6, 'Carmen',    'carmen',    'f');
  AddVoice(14, 7, 'Ming',      'ming',      'mg');
  AddVoice(15, 7, 'MingBig5',  'mingbig5',  'm');
end;

function InitializeSetup(): Boolean;
begin
  InitVoiceTables;
  Result := True;
end;

function IsLangSelected(Index: Integer): Boolean;
begin
  Result := WizardIsComponentSelected('lang\' + LangComp[Index]);
end;

function IsVoiceSelected(Index: Integer): Boolean;
begin
  Result := WizardIsComponentSelected('lang\' + VoiceComp[Index]);
end;

function VoicesInLanguage(Lang: Integer): Integer;
var
  i: Integer;
begin
  Result := 0;
  for i := 0 to VoiceCount - 1 do
    if (VoiceLang[i] = Lang) and IsVoiceSelected(i) then
      Result := Result + 1;
end;

// An empty selection would install a voiceless engine, and a language with no
// voice would occupy disk space nothing can reach.  Say so on the components
// page rather than letting the wizard finish and leaving the user hunting for a
// voice that was never installed.
function NextButtonClick(CurPageID: Integer): Boolean;
var
  i, Total: Integer;
begin
  Result := True;
  if CurPageID <> wpSelectComponents then
    exit;

  Total := 0;
  for i := 0 to LangCount - 1 do
    if IsLangSelected(i) then
    begin
      if VoicesInLanguage(i) = 0 then
      begin
        MsgBox('Please select at least one voice for ' + LangName[i]
             + ', or clear ' + LangName[i] + ' altogether.', mbError, MB_OK);
        Result := False;
        exit;
      end;
      Total := Total + VoicesInLanguage(i);
    end;

  if Total = 0 then
  begin
    MsgBox('Please select at least one language and one voice.', mbError, MB_OK);
    Result := False;
  end;
end;

// Delete one voice's channel templates: x<lang>.08<suffix>.chn together with its
// 11025 Hz and e-mail-preprocessing variants.  Not every voice has all four.
procedure DeleteVoiceChannels(Voice: Integer);
var
  Stem: String;
begin
  Stem := ExpandConstant('{app}\engine\data\chfiles\x') + LangComp[VoiceLang[Voice]] + '.';
  DeleteFile(Stem + '08' + VoiceSuffix[Voice] + '.chn');
  DeleteFile(Stem + '08' + VoiceSuffix[Voice] + 'e.chn');
  DeleteFile(Stem + '11' + VoiceSuffix[Voice] + '.chn');
  DeleteFile(Stem + '11' + VoiceSuffix[Voice] + 'e.chn');
end;

// Inno only ever adds files, so installing over a wider installation would leave
// the languages and voices that have just been cleared on disk - and the engine
// would go on speaking them.  Remove them before the new files are copied.
procedure RemoveClearedVoices;
var
  i: Integer;
  Engusg: String;
begin
  if not DirExists(ExpandConstant('{app}')) then
    exit;

  for i := 0 to LangCount - 1 do
    if not IsLangSelected(i) then
    begin
      DelTree(ExpandConstant('{app}\engine\data\languages\') + LangComp[i], True, True, True);
      DeleteFile(ExpandConstant('{app}\engine\x') + LangComp[i] + '.inton');
    end;

  for i := 0 to VoiceCount - 1 do
    if not IsVoiceSelected(i) then
      DeleteVoiceChannels(i);

  // US English is the only language with per-voice inventories; everywhere else
  // both voices are rendered from the same one.
  Engusg := ExpandConstant('{app}\engine\data\languages\engusg\bengusg');
  if not IsVoiceSelected(0) then
  begin
    DeleteFile(Engusg + '08m.00i');
    DeleteFile(Engusg + '08m.00n');
    DeleteFile(Engusg + '11m.00i');
    DeleteFile(Engusg + '11m.00n');
  end;
  if not IsVoiceSelected(1) then
  begin
    DeleteFile(Engusg + '08f.00i');
    DeleteFile(Engusg + '08f.00n');
    DeleteFile(Engusg + '11f.00i');
    DeleteFile(Engusg + '11f.00n');
  end;
end;

// The manifest src\installed_voices.cpp reads.  Anything absent from it is left
// out of the SAPI voice list and out of the configuration utility's combo boxes.
procedure WriteVoiceManifest;
var
  Lines: TArrayOfString;
  i, n: Integer;
begin
  SetArrayLength(Lines, LangCount + VoiceCount + 3);
  n := 0;
  Lines[n] := '; Written by Lucent TTS SAPI 5 Setup - do not edit by hand.'; n := n + 1;
  Lines[n] := '[languages]'; n := n + 1;
  for i := 0 to LangCount - 1 do
    if IsLangSelected(i) then
    begin
      Lines[n] := LangKey[i] + '=1';
      n := n + 1;
    end;
  Lines[n] := '[voices]'; n := n + 1;
  for i := 0 to VoiceCount - 1 do
    if IsVoiceSelected(i) then
    begin
      Lines[n] := VoiceKey[i] + '=1';
      n := n + 1;
    end;
  SetArrayLength(Lines, n);

  if not SaveStringsToFile(ExpandConstant('{app}\voices.ini'), Lines, False) then
    RaiseException('Unable to write the voice list (voices.ini).');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  i: Integer;
begin
  if CurStep = ssInstall then
  begin
    RemoveClearedVoices;
    DeleteFile(ExpandConstant('{app}\voices.ini'));
  end;

  if CurStep = ssPostInstall then
  begin
    WriteVoiceManifest;
    Log('Lucent TTS SAPI 5: files installed to ' + ExpandConstant('{app}'));
    for i := 0 to LangCount - 1 do
      if IsLangSelected(i) then
        Log('Lucent TTS SAPI 5: ' + LangName[i] + ', ' + IntToStr(VoicesInLanguage(i)) + ' voice(s)');
    Log('Lucent TTS SAPI 5: wrapper logs live in ' + ExpandConstant('{localappdata}') + '\LucentSAPI\logs');
  end;
end;

procedure InitializeUninstallProgressForm();
begin
  Log('Lucent TTS SAPI 5: uninstalling');
end;
