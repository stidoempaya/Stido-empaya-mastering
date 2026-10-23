; Installateur Windows de STIDO Empaya Mastering (Inno Setup 6)
; Compilé par GitHub Actions : iscc installer\windows.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define AppName "STIDO Empaya Mastering"
#define Artefacts "..\build\StidoEmpayaMastering_artefacts\Release"

[Setup]
AppId={{6E2B8F4A-3C1D-4E7B-9A52-5D0F1C8E7A31}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=STIDO
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
OutputDir=Output
OutputBaseFilename=STIDO-Empaya-Mastering-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Types]
Name: "full"; Description: "Installation complète"
Name: "custom"; Description: "Installation personnalisée"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Plugin VST3 (FL Studio, Ableton Live, Cubase, Studio One, Reaper…)"; Types: full custom
Name: "app"; Description: "Application autonome"; Types: full

[Files]
Source: "{#Artefacts}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\{#AppName}.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Components: app
Name: "{group}\Désinstaller {#AppName}"; Filename: "{uninstallexe}"

[Messages]
french.WelcomeLabel2=Cet assistant installe le plugin de mastering [name/ver] sur votre ordinateur.%n%nFermez votre logiciel de musique avant de continuer.
french.FinishedLabel=STIDO Empaya Mastering est installé.%n%nRelancez votre logiciel de musique et cherchez « STIDO » dans la liste des plugins (au besoin, relancez l'analyse des plugins dans ses préférences).
