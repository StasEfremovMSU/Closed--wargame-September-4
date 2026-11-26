[Setup]
AppId=AgeOfBattles.Win64
AppName=Эпоха битв
AppVersion={#ReleaseVersion}
AppPublisher=Эпоха битв
DefaultDirName={localappdata}\Programs\AgeOfBattles
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\AgeOfBattlesStas.exe
OutputDir={#SetupOutput}
OutputBaseFilename={#SetupName}
VersionInfoVersion={#ReleaseVersion}
VersionInfoDescription=Установщик «Эпоха битв»
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
WizardStyle=modern
Compression=lzma2/fast
SolidCompression=yes
RestartApplications=no

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
russian.Play=Запустить «Эпоху битв»
russian.RuntimeStartError=Не удалось запустить установку Microsoft Visual C++ x64. Подтвердите запрос Windows для установки компонента.%n%n%1
russian.RuntimeInstallError=Microsoft Visual C++ x64 не установился. Код ошибки %1.
russian.RuntimeRestart=Windows нужно перезагрузить для завершения установки Microsoft Visual C++ x64. После перезагрузки запустите этот установщик ещё раз.
english.Play=Launch Age of Battles
english.RuntimeStartError=Microsoft Visual C++ x64 setup could not start. Approve the Windows elevation prompt.%n%n%1
english.RuntimeInstallError=Microsoft Visual C++ x64 setup did not complete. Error code %1.
english.RuntimeRestart=Windows must restart to finish installing Microsoft Visual C++ x64. Run this setup again after restarting.

[Files]
Source: "{#VcRuntime}"; DestName: "vc_redist.x64.exe"; Flags: dontcopy
Source: "{#GamePayload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs solidbreak

[Icons]
Name: "{userprograms}\Эпоха битв"; Filename: "{app}\AgeOfBattlesStas.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\AgeOfBattlesStas.exe"; WorkingDir: "{app}"; Description: "{cm:Play}"; Flags: nowait postinstall skipifsilent; Check: CanLaunchGame

[Code]
const
  RuntimeKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';

var
  RuntimeNeedsRestart: Boolean;

function HasRequiredVCRuntime: Boolean;
var
  Installed: Cardinal;
  VersionText: String;
  InstalledVersion, RequiredVersion: Int64;
begin
  Result := False;
  if not RegQueryDWordValue(HKLM64, RuntimeKey, 'Installed', Installed) or (Installed <> 1) then
    Exit;
  if not RegQueryStringValue(HKLM64, RuntimeKey, 'Version', VersionText) then
    Exit;
  if (Length(VersionText) > 0) and (Lowercase(Copy(VersionText, 1, 1)) = 'v') then
    Delete(VersionText, 1, 1);
  if StrToVersion(VersionText, InstalledVersion) and StrToVersion('{#VcVersion}', RequiredVersion) then
    Result := ComparePackedVersion(InstalledVersion, RequiredVersion) >= 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  RuntimePath: String;
  Code: Integer;
begin
  Result := '';
  if HasRequiredVCRuntime then
  begin
    Log('Sufficient Microsoft Visual C++ x64 runtime already installed.');
    Exit;
  end;

  Log('Installing bundled Microsoft Visual C++ x64 prerequisite.');
  try
    ExtractTemporaryFile('vc_redist.x64.exe');
  except
    Result := FmtMessage(CustomMessage('RuntimeStartError'), [GetExceptionMessage]);
    Exit;
  end;
  RuntimePath := ExpandConstant('{tmp}\vc_redist.x64.exe');
  if not ShellExec('runas', RuntimePath, '/install /quiet /norestart',
    ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, Code) then
  begin
    Result := FmtMessage(CustomMessage('RuntimeStartError'), [SysErrorMessage(Code)]);
    Exit;
  end;
  Log(Format('Microsoft Visual C++ setup exit code: %d.', [Code]));

  RuntimeNeedsRestart := (Code = 3010) or (Code = 1641);
  if HasRequiredVCRuntime then
    Exit;
  if RuntimeNeedsRestart then
  begin
    NeedsRestart := True;
    Result := CustomMessage('RuntimeRestart');
    Exit;
  end;
  Result := FmtMessage(CustomMessage('RuntimeInstallError'), [IntToStr(Code)]);
end;

function CanLaunchGame: Boolean;
begin
  Result := not RuntimeNeedsRestart;
end;

function NeedRestart: Boolean;
begin
  Result := RuntimeNeedsRestart;
end;
