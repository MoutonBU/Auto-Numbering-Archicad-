; Inno Setup script: Windows installer for the AutoNumber Archicad add-on.
; The build workflow passes:  /DAcVer=28  /DApx=<full path to AutoNumber.apx>

#ifndef AcVer
  #define AcVer "28"
#endif
#ifndef Apx
  #define Apx "AutoNumber.apx"
#endif

[Setup]
AppId={{6F2B8C1E-4D3A-4E57-9B0A-5A7C1D2E9F41}
AppName=AutoNumber for Archicad {#AcVer}
AppVersion=1.0.0
AppPublisher=AutoNumber
DefaultDirName={code:GetAddOnsDir}\AutoNumber
DirExistsWarning=no
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
OutputDir=Output
OutputBaseFilename=AutoNumber-Setup-Archicad{#AcVer}
UninstallDisplayName=AutoNumber for Archicad {#AcVer}
WizardStyle=modern

[Files]
Source: "{#Apx}"; DestDir: "{app}"; Flags: ignoreversion

[Messages]
FinishedLabel=AutoNumber is installed.%n%nRestart Archicad {#AcVer}, then place an Object or Lamp: it gets the next number in its Element ID.%n%nSwitch it on or off with the menu command "Auto-Number Placed Elements".

[Code]
function GetAddOnsDir(Param: string): string;
var
  A, B: string;
begin
  A := ExpandConstant('{commonpf64}\GRAPHISOFT\ARCHICAD {#AcVer}\Add-Ons');
  B := ExpandConstant('{commonpf64}\GRAPHISOFT\Archicad {#AcVer}\Add-Ons');
  if DirExists(A) then
    Result := A
  else if DirExists(B) then
    Result := B
  else
    Result := B;
end;

function InitializeSetup(): Boolean;
var
  A, B: string;
begin
  A := ExpandConstant('{commonpf64}\GRAPHISOFT\ARCHICAD {#AcVer}');
  B := ExpandConstant('{commonpf64}\GRAPHISOFT\Archicad {#AcVer}');
  Result := True;
  if (not DirExists(A)) and (not DirExists(B)) then
    Result := MsgBox('Archicad {#AcVer} was not found in the default location.' + #13#10 +
      'You can pick its Add-Ons folder on the next page. Continue?', mbConfirmation, MB_YESNO) = IDYES;
end;
