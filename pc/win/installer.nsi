; Windows installer for the DW3 PC port (NSIS 3, Modern UI 2).
;
; Built by pc/win/make_installer.sh, which stages the files under STAGE and passes
; -DSTAGE=<dir> -DOUTFILE=<exe> -DVERSION=<text>. The installer ships the port only:
; the player points the game at their own disc on the first run (disc picker).
;
; A page asks for the player's disc image (written to settings.ini's [disc] path, so the
; first run starts without the picker) and, optionally, a PS1 memory card from an
; emulator (copied into the import folder, where the game imports it once); both may stay empty. /DISC=<file> and
; /CARD=<file> on the command line fill them (they are how a silent install sets them).
;
; Per-user install (no administrator prompt): %LOCALAPPDATA%\Programs\DW3 Port.
; Without portable.txt the game keeps settings, saves, imports and logs in
; %APPDATA%\dw3port\dw3\; uninstalling never touches that folder (the saves).

Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!ifndef STAGE
  !error "STAGE is not defined (run pc/win/make_installer.sh)"
!endif
!ifndef OUTFILE
  !define OUTFILE "DW3Port-Setup.exe"
!endif
!ifndef VERSION
  !define VERSION "beta"
!endif

!define APPNAME "DW3 Port"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\DW3Port"
!define DATADIR "$APPDATA\dw3port\dw3"
!define CARD_SIZE 131072

Var DiscPath
Var CardPath
Var DiscField
Var CardField

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APPNAME}"
InstallDirRegKey HKCU "Software\DW3Port" "InstallDir"
RequestExecutionLevel user

!define MUI_ABORTWARNING
; art made from pc/win/installer.png by installer_art.py (make_installer.sh)
!define MUI_ICON "${STAGE}\art\icon.ico"
!define MUI_UNICON "${STAGE}\art\icon.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${STAGE}\art\welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${STAGE}\art\welcome.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${STAGE}\art\header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "${STAGE}\art\header.bmp"
!define MUI_FINISHPAGE_RUN "$INSTDIR\DW3.exe"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
Page custom GamePageCreate GamePageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; the first language is the fallback; the installer picks the system's when it has it
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

LangString SecGame ${LANG_ENGLISH} "Game"
LangString SecGame ${LANG_SPANISH} "Juego"
LangString SecDesktop ${LANG_ENGLISH} "Desktop shortcut"
LangString SecDesktop ${LANG_SPANISH} "Acceso directo en el escritorio"
LangString LinkData ${LANG_ENGLISH} "DW3 Port - Saves and logs"
LangString LinkData ${LANG_SPANISH} "DW3 Port - Partidas y registros"
LangString GameTitle ${LANG_ENGLISH} "Your game"
LangString GameTitle ${LANG_SPANISH} "Tu juego"
LangString GameSub ${LANG_ENGLISH} "Choose your disc and, if you have one, your memory card."
LangString GameSub ${LANG_SPANISH} "Elige tu disco y, si tienes una, tu tarjeta de memoria."
LangString DiscLabel ${LANG_ENGLISH} "Disc image of Digimon World 3 (Europe), .cue or .bin. If you leave it empty, the game asks for it the first time it starts."
LangString DiscLabel ${LANG_SPANISH} "Imagen del disco de Digimon World 3 (Europa), .cue o .bin. Si lo dejas vacío, el juego lo pide la primera vez que se abre."
LangString CardLabel ${LANG_ENGLISH} "Optional: a memory card from an emulator (.mcd or .mcr, e.g. DuckStation or ePSXe) to continue your saves."
LangString CardLabel ${LANG_SPANISH} "Opcional: una tarjeta de memoria de un emulador (.mcd o .mcr, como DuckStation o ePSXe) para continuar tus partidas."
LangString Browse ${LANG_ENGLISH} "Browse..."
LangString Browse ${LANG_SPANISH} "Examinar..."
LangString DiscFilter ${LANG_ENGLISH} "Disc image (*.cue;*.bin)|*.cue;*.bin|All files|*.*"
LangString DiscFilter ${LANG_SPANISH} "Imagen de disco (*.cue;*.bin)|*.cue;*.bin|Todos los archivos|*.*"
LangString CardFilter ${LANG_ENGLISH} "Memory card (*.mcd;*.mcr)|*.mcd;*.mcr|All files|*.*"
LangString CardFilter ${LANG_SPANISH} "Tarjeta de memoria (*.mcd;*.mcr)|*.mcd;*.mcr|Todos los archivos|*.*"
LangString DiscMissing ${LANG_ENGLISH} "The disc image does not exist."
LangString DiscMissing ${LANG_SPANISH} "La imagen del disco no existe."
LangString DiscType ${LANG_ENGLISH} "The disc image must be a .cue or .bin file."
LangString DiscType ${LANG_SPANISH} "La imagen del disco debe ser un archivo .cue o .bin."
LangString CardMissing ${LANG_ENGLISH} "The memory card file does not exist."
LangString CardMissing ${LANG_SPANISH} "El archivo de la tarjeta de memoria no existe."
LangString CardBad ${LANG_ENGLISH} "That file is not a PS1 memory card (it must be a 128 KB raw card, .mcd or .mcr)."
LangString CardBad ${LANG_SPANISH} "Ese archivo no es una tarjeta de memoria de PS1 (debe ser una tarjeta de 128 KB, .mcd o .mcr)."

Function .onInit
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/DISC=" $DiscPath
  ClearErrors
  ${GetOptions} $0 "/CARD=" $CardPath
  ClearErrors
FunctionEnd

Function GamePageCreate
  !insertmacro MUI_HEADER_TEXT "$(GameTitle)" "$(GameSub)"
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 20u "$(DiscLabel)"
  Pop $0
  ${NSD_CreateText} 0 22u 76% 12u "$DiscPath"
  Pop $DiscField
  ${NSD_CreateButton} 78% 21u 22% 14u "$(Browse)"
  Pop $0
  ${NSD_OnClick} $0 BrowseDisc

  ${NSD_CreateLabel} 0 50u 100% 20u "$(CardLabel)"
  Pop $0
  ${NSD_CreateText} 0 72u 76% 12u "$CardPath"
  Pop $CardField
  ${NSD_CreateButton} 78% 71u 22% 14u "$(Browse)"
  Pop $0
  ${NSD_OnClick} $0 BrowseCard

  nsDialogs::Show
FunctionEnd

Function BrowseDisc
  ${NSD_GetText} $DiscField $0
  nsDialogs::SelectFileDialog open "$0" "$(DiscFilter)"
  Pop $0
  ${If} $0 != ""
    ${NSD_SetText} $DiscField $0
  ${EndIf}
FunctionEnd

Function BrowseCard
  ${NSD_GetText} $CardField $0
  nsDialogs::SelectFileDialog open "$0" "$(CardFilter)"
  Pop $0
  ${If} $0 != ""
    ${NSD_SetText} $CardField $0
  ${EndIf}
FunctionEnd

; checks both choices; Abort keeps the player on the page
Function GamePageLeave
  ${NSD_GetText} $DiscField $DiscPath
  ${NSD_GetText} $CardField $CardPath

  ${If} $DiscPath != ""
    ${IfNot} ${FileExists} "$DiscPath"
      MessageBox MB_ICONEXCLAMATION "$(DiscMissing)"
      Abort
    ${EndIf}
    ${GetFileExt} "$DiscPath" $0
    ${If} $0 != "cue"
    ${AndIf} $0 != "bin"
      MessageBox MB_ICONEXCLAMATION "$(DiscType)"
      Abort
    ${EndIf}
  ${EndIf}

  ${If} $CardPath != ""
    ${IfNot} ${FileExists} "$CardPath"
      MessageBox MB_ICONEXCLAMATION "$(CardMissing)"
      Abort
    ${EndIf}
    ; a raw PS1 card: 128 KB, starting with "MC"
    FileOpen $1 "$CardPath" r
    FileSeek $1 0 END $2
    FileSeek $1 0 SET
    FileReadByte $1 $3
    FileReadByte $1 $4
    FileClose $1
    ${If} $2 != ${CARD_SIZE}
    ${OrIf} $3 != 77
    ${OrIf} $4 != 67
      MessageBox MB_ICONEXCLAMATION "$(CardBad)"
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "!$(SecGame)" SecGameId
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${STAGE}\DW3.exe"
  File "${STAGE}\DW3-debug.exe"
  File "${STAGE}\SDL2.dll"
  File "${STAGE}\OpenAL32.dll"
  File "${STAGE}\README.txt"
  File "${STAGE}\LEEME.txt"
  File "${STAGE}\THIRD-PARTY.txt"
  File /r "${STAGE}\licenses"
  File /r "${STAGE}\localization"

  ; the data folder the game creates on its first run; made here so the shortcut has a target
  CreateDirectory "${DATADIR}"

  ; the disc: an existing settings.ini keeps the player's other settings; a new one only
  ; needs the [disc] section (the game writes every default around it on its first run)
  ${If} $DiscPath != ""
    ${If} ${FileExists} "${DATADIR}\settings.ini"
      WriteINIStr "${DATADIR}\settings.ini" "disc" "path" "$DiscPath"
    ${Else}
      FileOpen $0 "${DATADIR}\settings.ini" w
      FileWrite $0 "[disc]$\r$\npath = $DiscPath$\r$\n"
      FileClose $0
    ${EndIf}
  ${EndIf}

  ; the memory card goes into import\ (ADR-010): the game imports every .mcd there once; a card already there
  ; with this name is kept as installer.mcd.bak, never lost
  ${If} $CardPath != ""
    CreateDirectory "${DATADIR}\import"
    ${If} ${FileExists} "${DATADIR}\import\installer.mcd"
      Delete "${DATADIR}\import\installer.mcd.bak"
      Rename "${DATADIR}\import\installer.mcd" "${DATADIR}\import\installer.mcd.bak"
    ${EndIf}
    CopyFiles /SILENT "$CardPath" "${DATADIR}\import\installer.mcd"
  ${EndIf}

  ; shortcuts start in $INSTDIR (the current SetOutPath), where the game looks for its localization folder
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\DW3.exe"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\$(LinkData).lnk" "${DATADIR}" "" "$INSTDIR\DW3.exe" 0
  ; the same link inside the install folder, so the saves are one click from the game
  CreateShortCut "$INSTDIR\$(LinkData).lnk" "${DATADIR}" "" "$INSTDIR\DW3.exe" 0

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\DW3Port" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\DW3.exe"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "$(SecDesktop)" SecDesktopId
  SetOutPath "$INSTDIR"
  CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\DW3.exe"
SectionEnd

; removes the program only: the saves, settings and logs in %APPDATA%\dw3port stay
Section "Uninstall"
  Delete "$INSTDIR\DW3.exe"
  Delete "$INSTDIR\DW3-debug.exe"
  Delete "$INSTDIR\SDL2.dll"
  Delete "$INSTDIR\OpenAL32.dll"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\LEEME.txt"
  Delete "$INSTDIR\THIRD-PARTY.txt"
  Delete "$INSTDIR\Uninstall.exe"
  Delete "$INSTDIR\*.lnk"
  RMDir /r "$INSTDIR\licenses"
  RMDir /r "$INSTDIR\localization"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APPNAME}\*.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  Delete "$DESKTOP\${APPNAME}.lnk"
  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "Software\DW3Port"
SectionEnd
