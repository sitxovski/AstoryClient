; Copyright (c) 2026 @sitxovski (Discord)
; AStoryRemake.iss — установщик AStory Remake (Inno Setup 6.5), 17.10.2026.
; Ставит лаунчер в выбранную папку; игру лаунчер скачивает сам в подпапку Game (config::kGameSubdir).
; Сразу: рантайм Visual C++ 2015-2022 x64 (если не установлен или старее), служба Easy Anti-Cheat, записи в реестре,
; ярлык «Astory» на рабочем столе и в «Пуске». Удаление: лаунчер, скачанная игра, служба EAC, записи в реестре.
; Собирать через AstoryClient\DEPLOY\Build\6_Installer_Build.bat (передаёт AppVer, GameDir и BootExe). Пути — относительно этого файла.
; ★ 25.10.2026 [ИСПРАВЛЕНИЕ] «Error 5» на машинах, где запуск программ из %TEMP% запрещён (AppLocker/SRP/NTFS): установщик
;   собирается БЕЗ загрузчика (UseSetupLdr=no — тот распаковывал setup в %TEMP%\is-*.tmp и запускал оттуда); его файлы
;   приклеивает к обёртке Boot\AsdaSetupBoot.exe deploy.py, обёртка разворачивает их в папку, где запуск разрешён.
;   Удаление — через {app}\Setup\AStoryUninstall.exe (та же обёртка без нагрузки): сама повышает права и запускает
;   unins000.exe с TEMP в закрытой папке (деинсталлятор Inno копирует себя в %TEMP% и запускает копию).
; ★ 25.10.2026 ревизия (REVIEW.md): всё, что запускается с правами администратора (деинсталлятор и его данные, помощник
;   удаления, установщик службы EAC, временный vc_redist), лежит в {app}\Setup — папке ТОЛЬКО для администраторов и SYSTEM
;   (владелец — Администраторы, наследование снято); пользователям «изменение» — только {app} с лаунчером и Game, и только
;   если папку {app} создал установщик или там уже наш лаунчер (Н2). vc_redist — в {app}\Setup\tmp (М1). Права — icacls,
;   а не «Permissions:» (её Inno делает помощником _setup64.tmp из %TEMP%). Коды vc_redist и службы EAC проверяются (М5).
;   Отчёт: Reports\launcher_temp\REPORT.md.
;
; Ключи ISCC для стенда проверки без UAC (Reports\launcher_temp\tools\build_test.py); боевая сборка их не передаёт:
;   /DPrivileges=lowest /DRegRoot=HKCU /DRegSubkey=… /DNoIcons /DNoLangReg /DForceVcRedist /DVcArgs=… /DEacInstallVerb=…
;   /DEacUninstallVerb=… /DLockExtraGrant=… (лишняя запись в ACL {app}\Setup — тестовому пользователю без прав админа)

#ifndef AppVer
  #define AppVer "1.0.0"
#endif
#ifndef GameDir
  #define GameDir "..\GAME"
#endif
#ifndef BootExe
  #define BootExe "Boot\build\AsdaSetupBoot.exe"
#endif
#ifndef Privileges
  #define Privileges "admin"
#endif
#ifndef RegRoot
  #define RegRoot "HKLM64"
#endif
#ifndef RegSubkey
  #define RegSubkey "Software\AStoryRemake"
#endif
#ifndef VcArgs
  #define VcArgs "/install /quiet /norestart"
#endif
#ifndef EacInstallVerb
  #define EacInstallVerb "install"
#endif
#ifndef EacUninstallVerb
  #define EacUninstallVerb "uninstall"
#endif
#ifndef LockExtraGrant
  #define LockExtraGrant ""
#endif
#define AppName "AStory Remake"
#define AppExe "AsdaLauncher.exe"
#define ShortcutName "Astory"
#define EacProductId "@@ASTORY_EOS_PRODUCT_ID@@"
#define VcRedist GameDir + "\Redist\vc_redist.x64.exe"
#define VcVer GetVersionNumbersString(VcRedist)
; AppId установщика: свой GUID у каждой сборки-форка (иначе один деинсталлятор и запись реестра с чужим установщиком).
;   Подставляет _setup\configure (ASTORY_INSTALLER_APP_GUID) — GUID БЕЗ фигурных скобок, например 1A2B3C4D-....;
;   скобки добавляются ниже: AppId={{{#AppGuid}} ({{ — литерал «{» в Inno Setup) и ключ Uninstall\{GUID}_is1.
#define AppGuid "@@ASTORY_INSTALLER_APP_GUID@@"
#if Pos("@@", AppGuid) == 1
  #error AppGuid не задан: запустите _setup\configure.bat (ASTORY_INSTALLER_APP_GUID, GUID без скобок)
#endif
#define UninstallerExe "AStoryUninstall.exe"
; служебная папка только для администраторов: деинсталлятор, помощник удаления, установщик службы EAC, временный vc_redist
#define SetupSub "Setup"

[Setup]
AppId={{{#AppGuid}}
AppName={#AppName}
AppVersion={#AppVer}
AppVerName={#AppName} {#AppVer}
AppPublisher=AStory Remake
AppPublisherURL=https://@@ASTORY_DOMAIN@@
AppSupportURL=https://@@ASTORY_DOMAIN@@
AppUpdatesURL=https://@@ASTORY_DOMAIN@@
AppCopyright=AStory Remake
VersionInfoVersion={#AppVer}.0
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
DefaultDirName={autopf}\AStory Remake
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
UsePreviousAppDir=yes
PrivilegesRequired={#Privileges}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
WizardSizePercent=110
WizardImageFile=Branding\wizard_164x314.bmp,Branding\wizard_192x386.bmp,Branding\wizard_246x459.bmp,Branding\wizard_273x556.bmp,Branding\wizard_328x604.bmp,Branding\wizard_355x700.bmp,Branding\wizard_410x797.bmp
WizardSmallImageFile=Branding\small_55x55.bmp,Branding\small_64x68.bmp,Branding\small_83x80.bmp,Branding\small_92x97.bmp,Branding\small_110x106.bmp,Branding\small_119x123.bmp,Branding\small_138x140.bmp
SetupIconFile=Branding\AStory.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
; деинсталлятор и его данные — в закрытой {app}\Setup (до 1.0.5 лежали в {app}, открытой на запись пользователям)
UninstallFilesDir={app}\{#SetupSub}
LicenseFile={#GameDir}\Legal\EULA.txt
OutputDir=Output
OutputBaseFilename=AStoryRemake_Setup_{#AppVer}
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
SetupMutex=AStoryRemakeSetupMutex
; без загрузчика: setup.exe + -0.bin/-1.bin, их приклеивает к Boot\AsdaSetupBoot.exe deploy.py (см. шапку)
UseSetupLdr=no

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
ru.WelcomeLabel1=Добро пожаловать в [name]!
ru.WelcomeLabel2=Мастер установит лаунчер AStory Remake на этот компьютер.%n%nСаму игру лаунчер скачает при первом запуске и дальше будет обновлять сам.%n%nПеред продолжением рекомендуется закрыть другие программы.
en.WelcomeLabel1=Welcome to [name]!
en.WelcomeLabel2=This wizard will install the AStory Remake launcher on your computer.%n%nThe launcher downloads the game on first start and keeps it up to date.%n%nIt is recommended that you close all other applications before continuing.
ru.FinishedHeadingLabel=AStory Remake установлен
ru.FinishedLabelNoIcons=Лаунчер установлен. При первом запуске он скачает игру (около 6 ГБ).
ru.FinishedLabel=Лаунчер установлен. При первом запуске он скачает игру (около 6 ГБ). Запускайте игру ярлыком «Astory».
en.FinishedHeadingLabel=AStory Remake is installed
en.FinishedLabelNoIcons=The launcher is installed. On first start it will download the game (about 6 GB).
en.FinishedLabel=The launcher is installed. On first start it will download the game (about 6 GB). Start the game with the "Astory" shortcut.

[CustomMessages]
ru.DesktopIcon=Создать ярлык «Astory» на рабочем столе
en.DesktopIcon=Create the "Astory" desktop shortcut
ru.Shortcuts=Ярлыки:
en.Shortcuts=Shortcuts:
ru.Prereqs=Установка Microsoft Visual C++ Runtime...
en.Prereqs=Installing Microsoft Visual C++ Runtime...
ru.Eac=Установка службы Easy Anti-Cheat...
en.Eac=Installing the Easy Anti-Cheat service...
ru.Launch=Запустить AStory Remake
en.Launch=Launch AStory Remake
ru.PermFailed=Не удалось выдать пользователям право записи в папку %1. Лаунчер будет запрашивать права администратора при обновлении.
en.PermFailed=Could not grant users write access to the folder %1. The launcher will ask for administrator rights when updating.
ru.PermForeign=Папка %1 уже существовала и содержит посторонние файлы, поэтому право записи пользователям выдано только на подпапку Game. Самообновление лаунчера будет запрашивать права администратора.
en.PermForeign=The folder %1 already existed and contains other files, so users were granted write access only to its Game subfolder. Launcher self-updates will ask for administrator rights.
ru.LockFailed=Не удалось закрыть служебную папку %1 от изменения пользователями (код %2). Установка прервана.
en.LockFailed=Could not protect the service folder %1 from changes by users (code %2). Setup aborted.
ru.VcFailed=Не удалось установить Microsoft Visual C++ Runtime (код %1). Игра может не запуститься — установите его вручную с сайта Microsoft (vc_redist.x64.exe) или запустите установку ещё раз.
en.VcFailed=Microsoft Visual C++ Runtime could not be installed (code %1). The game may not start — install it manually from Microsoft (vc_redist.x64.exe) or run Setup again.
ru.EacFailed=Не удалось установить службу Easy Anti-Cheat (код %1). Лаунчер попробует установить её снова при нажатии «Играть».
en.EacFailed=The Easy Anti-Cheat service could not be installed (code %1). The launcher will try to install it again when you press "Play".
ru.EacUninstFailed=Не удалось удалить регистрацию игры в службе Easy Anti-Cheat (код %1). Остальное удаление продолжится.
en.EacUninstFailed=Could not remove the game from the Easy Anti-Cheat service (code %1). The rest of the uninstall will continue.

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; GroupDescription: "{cm:Shortcuts}"

[Dirs]
; Лаунчер скачивает игру в {app}\Game и обновляет сам себя — без прав администратора; право записи пользователям выдаёт
; GrantUsersModify в ssPostInstall (icacls из System32: «Permissions:» Inno делает помощником из %TEMP%).
; {app}\Setup закрывается сразу после создания — ДО того, как туда лягут деинсталлятор и прочее (LockSetupDir).
Name: "{app}"
Name: "{app}\Game"
Name: "{app}\{#SetupSub}"; AfterInstall: LockSetupDir

[Files]
Source: "{#GameDir}\Launcher\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#GameDir}\Legal\EULA.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#GameDir}\Redist\EasyAntiCheat\*"; DestDir: "{app}\{#SetupSub}\Redist\EasyAntiCheat"; Flags: ignoreversion recursesubdirs createallsubdirs
; помощник удаления (обёртка без нагрузки): на него указывают UninstallString и ярлык «Удалить» (см. RedirectUninstall)
Source: "{#BootExe}"; DestDir: "{app}\{#SetupSub}"; DestName: "{#UninstallerExe}"; Flags: ignoreversion
Source: "{#VcRedist}"; DestDir: "{code:ExecTmp}"; Flags: deleteafterinstall; Check: VcRedistNeeded

[Icons]
#ifndef NoIcons
Name: "{autodesktop}\{#ShortcutName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Comment: "{#AppName}"; Tasks: desktopicon
Name: "{group}\{#ShortcutName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Comment: "{#AppName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{app}\{#SetupSub}\{#UninstallerExe}"; IconFilename: "{uninstallexe}"
#endif

[Registry]
; Сведения об установке (для лаунчера, поддержки и будущих установщиков патчей)
Root: {#RegRoot}; Subkey: "{#RegSubkey}"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey
Root: {#RegRoot}; Subkey: "{#RegSubkey}"; ValueType: string; ValueName: "GameDir"; ValueData: "{app}\Game"
Root: {#RegRoot}; Subkey: "{#RegSubkey}"; ValueType: string; ValueName: "LauncherExe"; ValueData: "{app}\{#AppExe}"
Root: {#RegRoot}; Subkey: "{#RegSubkey}"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVer}"
Root: {#RegRoot}; Subkey: "{#RegSubkey}"; ValueType: string; ValueName: "EacProductId"; ValueData: "{#EacProductId}"

[Run]
; vc_redist и служба EAC — в ssPostInstall (InstallPrerequisites): там проверяются их коды (ревизия М5)
#ifndef NoLangReg
; Язык лаунчера (0 — русский, 1 — английский) по языку установки — в реестр ТОГО пользователя, кто запустил установку
; (установщик работает от администратора, его HKCU может быть чужим), и только если язык ещё не выбирали.
Filename: "{cmd}"; Parameters: "/c reg query ""HKCU\Software\AStoryRemake\Launcher"" /v Lang >nul 2>&1 || reg add ""HKCU\Software\AStoryRemake\Launcher"" /v Lang /t REG_DWORD /d 0 /f >nul"; Languages: ru; Flags: runhidden waituntilterminated runasoriginaluser
Filename: "{cmd}"; Parameters: "/c reg query ""HKCU\Software\AStoryRemake\Launcher"" /v Lang >nul 2>&1 || reg add ""HKCU\Software\AStoryRemake\Launcher"" /v Lang /t REG_DWORD /d 1 /f >nul"; Languages: en; Flags: runhidden waituntilterminated runasoriginaluser
#endif
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Description: "{cm:Launch}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Game"
Type: files; Name: "{app}\*.old"
Type: files; Name: "{app}\*.new"
Type: filesandordirs; Name: "{app}\{#SetupSub}\tmp"
Type: dirifempty; Name: "{app}"

[Code]
const
  BrandDark = $000C1218;   { тёмный фон шапки (RGB 24,18,12 — как в Branding\make_branding.py) }
  BrandGold = $004EC1F2;   { золото логотипа (RGB 242,193,78) }
  BrandText = $00F0F0F0;   { светлый текст }

{ Нужен ли рантайм VC++: нет записи или установленная сборка старее входящей в установщик ({#VcVer}). }
function VcRedistNeeded: Boolean;
var
  Installed, Bld: Cardinal;
  BundledBld: Integer;
  S: String;
begin
#ifdef ForceVcRedist
  Result := True;
  Exit;
#endif
  S := '{#VcVer}';                                      { major.minor.build.rev }
  Delete(S, 1, Pos('.', S)); Delete(S, 1, Pos('.', S)); { build.rev }
  BundledBld := StrToIntDef(Copy(S, 1, Pos('.', S) - 1), 0);
  Result := True;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1) and
     RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Bld', Bld) then
    Result := Integer(Bld) < BundledBld;
end;

{ ★ 25.10.2026 [ИСПРАВЛЕНИЕ] запуск из %TEMP% запрещён (см. шапку): запускаемые временные файлы и TEMP/TMP дочерних
  процессов — в <app>\Setup\tmp (закрыта для пользователей, Program Files разрешён для запуска и стандартными правилами AppLocker). }
function SetEnvironmentVariable(lpName, lpValue: String): BOOL;
  external 'SetEnvironmentVariableW@kernel32.dll stdcall';
{ То же с lpValue = NULL — удалить переменную (если её не было до UseExecTemp) }
function UnsetEnvironmentVariable(lpName: String; lpValue: Cardinal): BOOL;
  external 'SetEnvironmentVariableW@kernel32.dll stdcall';
function GetFileAttributes(lpFileName: String): Cardinal;
  external 'GetFileAttributesW@kernel32.dll stdcall';

var
  ExecTmpDir: String;          { папка запускаемых временных файлов; '' — ещё не создана }
  SavedTemp, SavedTmp: String; { TEMP/TMP процесса Setup до UseExecTemp }
  AppDirOurs: Boolean;         { <app> создан установщиком или в нём уже наш лаунчер — можно открыть её на запись }

{ Запуск служебной программы из System32 со сбором её вывода в журнал установки; код — в Rc (-1 — не запустилась). }
function RunSys(Exe, Params: String; var Rc: Integer): Boolean;
begin
  Result := ExecAndLogOutput(ExpandConstant('{sys}\') + Exe, Params, '', SW_HIDE, ewWaitUntilTerminated, Rc, nil);
  if not Result then Rc := -1;
  Log(Format('%s %s → код %d', [Exe, Params, Rc]));
end;

{ Закрыть <app>\Setup: владелец — Администраторы, наследование снято, полный доступ — Администраторам и SYSTEM, чтение и
  запуск — Пользователям; вложенное только наследует. Ссылку на месте папки удаляем и создаём папку заново. Без этого
  деинсталлятор и установщик службы EAC (запускаются от администратора) могли бы подменить пользователи — установка
  прерывается (ревизия Н2). }
procedure LockSetupDir;
var
  Dir: String;
  Attr: Cardinal;
  Rc: Integer;
begin
  Dir := ExpandConstant('{app}\{#SetupSub}');
  Attr := GetFileAttributes(Dir);
  if (Attr <> $FFFFFFFF) and ((Attr and FILE_ATTRIBUTE_REPARSE_POINT) <> 0) then
  begin
    Log('На месте ' + Dir + ' ссылка — удаляю');
    RemoveDir(Dir);
    ForceDirectories(Dir);
  end;
  RunSys('icacls.exe', '"' + Dir + '" /setowner *S-1-5-32-544 /T /C /Q', Rc);
  RunSys('icacls.exe', '"' + Dir + '" /inheritance:r /grant:r *S-1-5-32-544:(OI)(CI)F *S-1-5-18:(OI)(CI)F ' +
         '*S-1-5-32-545:(OI)(CI)RX {#LockExtraGrant} /Q', Rc);
  if Rc <> 0 then
    RaiseException(FmtMessage(CustomMessage('LockFailed'), [Dir, IntToStr(Rc)]));
  RunSys('icacls.exe', '"' + Dir + '\*" /reset /T /C /Q', Rc);   { уже лежащее (обновление) — только наследует }
end;

{ Папка запускаемых временных файлов: <app>\Setup\tmp (наследует закрытые права <app>\Setup — ревизия М1). }
function ExecTmp(Param: String): String;
begin
  if ExecTmpDir = '' then
  begin
    ExecTmpDir := ExpandConstant('{app}\{#SetupSub}\tmp');
    if not ForceDirectories(ExecTmpDir) then
      RaiseException('Не создана папка ' + ExecTmpDir);
    Log('Папка запускаемых временных файлов: ' + ExecTmpDir);
  end;
  Result := ExecTmpDir;
end;

{ Перед запуском vc_redist и установщика службы EAC: их собственные временные exe — тоже в ExecTmp. }
procedure UseExecTemp;
begin
  SavedTemp := GetEnv('TEMP');
  SavedTmp := GetEnv('TMP');
  SetEnvironmentVariable('TEMP', ExecTmp(''));
  SetEnvironmentVariable('TMP', ExecTmp(''));
end;

{ После запуска: прежние TEMP/TMP (лаунчер, запущенный в конце, и игра не должны унаследовать удаляемую папку). }
procedure RestoreTemp;
begin
  if SavedTemp = '' then UnsetEnvironmentVariable('TEMP', 0) else SetEnvironmentVariable('TEMP', SavedTemp);
  if SavedTmp = '' then UnsetEnvironmentVariable('TMP', 0) else SetEnvironmentVariable('TMP', SavedTmp);
end;

{ Пустая ли папка }
function DirIsEmpty(Dir: String): Boolean;
var
  F: TFindRec;
begin
  Result := True;
  if FindFirst(AddBackslash(Dir) + '*', F) then
  try
    repeat
      if (F.Name <> '.') and (F.Name <> '..') then
      begin
        Result := False;
        Break;
      end;
    until not FindNext(F);
  finally
    FindClose(F);
  end;
end;

{ До начала установки (страница «Подготовка», вызывается и в тихом режиме; к ssInstall Inno уже создаёт <app>):
  можно ли открывать <app> на запись — новая/пустая папка или там уже наш лаунчер. }
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  App: String;
begin
  App := ExpandConstant('{app}');
  AppDirOurs := not DirExists(App) or DirIsEmpty(App) or FileExists(App + '\{#AppExe}');
  if AppDirOurs then
    Log('Папка установки ' + App + ': новая, пустая или наша — пользователям право записи на неё')
  else
    Log('Папка установки ' + App + ': уже есть и с посторонними файлами — пользователям право записи только на Game');
  Result := '';
end;

{ Перед установкой: убрать файлы старой раскладки (до 1.0.5: деинсталлятор и Redist лежали прямо в <app>, открытой на
  запись пользователям). }
procedure PrepareAppDir;
var
  App: String;
begin
  App := ExpandConstant('{app}');
  if FileExists(App + '\{#AppExe}') then
  begin
    DeleteFile(App + '\unins000.exe');
    DeleteFile(App + '\unins000.dat');
    DeleteFile(App + '\unins000.msg');
    DelTree(App + '\Redist', True, True, True);
  end;
end;

{ Есть ли у папки собственная (не унаследованная) запись «изменение с наследованием» — проверка после ненулевого кода
  icacls (например, файл в Game занят), имена групп в выводе icacls зависят от языка Windows, права — нет. }
function HasOwnModifyAce(Dir: String): Boolean;
var
  Rc, I: Integer;
  Acl: TExecOutput;
begin
  Result := False;
  if ExecAndCaptureOutput(ExpandConstant('{sys}\icacls.exe'), '"' + Dir + '"', '', SW_HIDE, ewWaitUntilTerminated, Rc, Acl) then
    for I := 0 to GetArrayLength(Acl.StdOut) - 1 do
      if (Pos('(OI)(CI)(M)', Acl.StdOut[I]) > 0) and (Pos('(I)', Acl.StdOut[I]) = 0) then
        Result := True;
end;

{ Пользователям (BUILTIN\Users, S-1-5-32-545) — «изменение» с наследованием: <app>, если она наша, иначе только <app>\Game
  (на посторонние файлы в общей папке широких прав не выдаём — ревизия Н2). <app>\Setup не затрагивается: у неё
  наследование снято. Ошибка — в журнал и сообщение: без этого права лаунчер обновляет игру только с правами администратора. }
procedure GrantUsersModify;
var
  Dir: String;
  Rc: Integer;
begin
  if AppDirOurs then
    Dir := ExpandConstant('{app}')
  else
  begin
    Dir := ExpandConstant('{app}\Game');
    SuppressibleMsgBox(FmtMessage(CustomMessage('PermForeign'), [ExpandConstant('{app}')]), mbInformation, MB_OK, IDOK);
  end;
  RunSys('icacls.exe', '"' + Dir + '" /grant *S-1-5-32-545:(OI)(CI)M /C /Q', Rc);
  if (Rc = 0) or HasOwnModifyAce(Dir) then
    Log('Права на запись пользователям: ' + Dir)
  else
    SuppressibleMsgBox(FmtMessage(CustomMessage('PermFailed'), [Dir]), mbError, MB_OK, IDOK);
end;

{ Удаление — через <app>\Setup\AStoryUninstall.exe (повышает права и запускает unins000.exe с TEMP в закрытой папке):
  подменяем строки удаления, которые Inno записал для «Приложений и возможностей». }
procedure RedirectUninstall;
var
  Key, Exe: String;
begin
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{' + '{#AppGuid}' + '}_is1';
  Exe := '"' + ExpandConstant('{app}\{#SetupSub}\{#UninstallerExe}') + '"';
  if RegKeyExists(HKA64, Key) then
  begin
    RegWriteStringValue(HKA64, Key, 'UninstallString', Exe);
    RegWriteStringValue(HKA64, Key, 'QuietUninstallString', Exe + ' /SILENT');
    Log('Строки удаления → ' + Exe);
  end else
    Log('Нет ключа удаления ' + Key + ' — строки удаления не подменены');
end;

{ vc_redist (если нужен) и служба EAC с проверкой кодов (ревизия М5): vc_redist — 0, 1638 (уже стоит новее), 3010/1641
  (нужна перезагрузка); EAC — код 0 и служба EasyAntiCheat_EOS зарегистрирована. Ошибка — журнал и сообщение RU/EN,
  установка продолжается (лаунчер сам доустанавливает службу EAC перед игрой). }
procedure InstallPrerequisites;
var
  Rc, ScRc: Integer;
begin
  if VcRedistNeeded then
  begin
    WizardForm.StatusLabel.Caption := CustomMessage('Prereqs');
    UseExecTemp;
    Log('vc_redist: ' + ExecTmp('') + '\vc_redist.x64.exe {#VcArgs}');
    if not Exec(ExecTmp('') + '\vc_redist.x64.exe', '{#VcArgs}', ExecTmp(''), SW_HIDE, ewWaitUntilTerminated, Rc) then
      Rc := -1;
    RestoreTemp;
    Log(Format('vc_redist: код %d', [Rc]));
    if (Rc <> 0) and (Rc <> 1638) and (Rc <> 3010) and (Rc <> 1641) then
      SuppressibleMsgBox(FmtMessage(CustomMessage('VcFailed'), [IntToStr(Rc)]), mbError, MB_OK, IDOK);
  end;
  WizardForm.StatusLabel.Caption := CustomMessage('Eac');
  UseExecTemp;
  if not Exec(ExpandConstant('{app}\{#SetupSub}\Redist\EasyAntiCheat\EasyAntiCheat_EOS_Setup.exe'),
              '{#EacInstallVerb} {#EacProductId}', ExecTmp(''), SW_HIDE, ewWaitUntilTerminated, Rc) then
    Rc := -1;
  RestoreTemp;
  RunSys('sc.exe', 'query EasyAntiCheat_EOS', ScRc);
  Log(Format('Easy Anti-Cheat: код %d, служба %d', [Rc, ScRc]));
  if (Rc <> 0) or (ScRc <> 0) then
    SuppressibleMsgBox(FmtMessage(CustomMessage('EacFailed'), [IntToStr(Rc)]), mbError, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    PrepareAppDir
  else if CurStep = ssPostInstall then
  begin
    GrantUsersModify;
    RedirectUninstall;
    InstallPrerequisites;
    if ExecTmpDir <> '' then
      DelTree(ExecTmpDir, True, True, True);
  end;
end;

{ Удаление: вторая фаза деинсталлятора работает из закрытой папки, куда её TEMP направил AStoryUninstall.exe, — туда же
  TEMP/TMP для установщика службы EAC. }
function InitializeUninstall: Boolean;
var
  Dir: String;
begin
  Dir := ExtractFileDir(ParamStr(0));
  SetEnvironmentVariable('TEMP', Dir);
  SetEnvironmentVariable('TMP', Dir);
  Log('TEMP/TMP удаления: ' + Dir);
  Result := True;
end;

{ Удаление: снять регистрацию игры в службе EAC (до удаления файлов) с проверкой кода (ревизия М5). }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Rc: Integer;
begin
  if CurUninstallStep <> usUninstall then Exit;
  if not Exec(ExpandConstant('{app}\{#SetupSub}\Redist\EasyAntiCheat\EasyAntiCheat_EOS_Setup.exe'),
              '{#EacUninstallVerb} {#EacProductId}', '', SW_HIDE, ewWaitUntilTerminated, Rc) then
    Rc := -1;
  Log(Format('Easy Anti-Cheat (удаление): код %d', [Rc]));
  if (Rc <> 0) and not UninstallSilent then
    MsgBox(FmtMessage(CustomMessage('EacUninstFailed'), [IntToStr(Rc)]), mbError, MB_OK);
end;

{ Фирменное оформление: тёмная шапка с золотым заголовком, тёмные страницы приветствия и завершения. }
procedure InitializeWizard;
begin
  WizardForm.MainPanel.Color := BrandDark;
  WizardForm.PageNameLabel.Font.Color := BrandGold;
  WizardForm.PageDescriptionLabel.Font.Color := BrandText;

  WizardForm.WelcomePage.Color := BrandDark;
  WizardForm.WelcomeLabel1.Font.Color := BrandGold;
  WizardForm.WelcomeLabel2.Font.Color := BrandText;

  WizardForm.FinishedPage.Color := BrandDark;
  WizardForm.FinishedHeadingLabel.Font.Color := BrandGold;
  WizardForm.FinishedLabel.Font.Color := BrandText;
  WizardForm.RunList.Color := BrandDark;
  WizardForm.RunList.Font.Color := BrandText;
end;
