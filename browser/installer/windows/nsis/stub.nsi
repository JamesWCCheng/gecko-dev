# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Required Plugins:
# AppAssocReg
# CertCheck
# InetBgDL
# ShellLink
# UAC

; Set verbosity to 3 (e.g. no script) to lessen the noise in the build logs
!verbose 3

SetDatablockOptimize on
SetCompress off
CRCCheck on

RequestExecutionLevel user

Unicode true
ManifestSupportedOS all
ManifestDPIAware true

!addplugindir ./

Var Dialog
Var Progressbar
Var ProgressbarMarqueeIntervalMS
Var LabelDownloading
Var LabelInstalling
Var LabelFreeSpace
Var CheckboxSetAsDefault
Var CheckboxShortcuts
Var CheckboxSendPing
Var CheckboxInstallMaintSvc
Var DroplistArch
Var DirRequest
Var ButtonBrowse
Var LabelBlurb1
Var LabelBlurb2
Var LabelBlurb3
Var BitmapBlurb1
Var BitmapBlurb2
Var BitmapBlurb3
Var HwndBitmapBlurb1
Var HwndBitmapBlurb2
Var HWndBitmapBlurb3

Var FontNormal
Var FontItalic
Var FontBlurb

Var WasOptionsButtonClicked
Var CanWriteToInstallDir
Var HasRequiredSpaceAvailable
Var IsDownloadFinished
Var DownloadSizeBytes
Var HalfOfDownload
Var DownloadReset
Var ExistingTopDir
Var SpaceAvailableBytes
Var InitialInstallDir
Var HandleDownload
Var CanSetAsDefault
Var InstallCounterStep
Var InstallStepSize
Var InstallTotalSteps
Var ProgressCompleted
Var ProgressTotal

Var ExitCode
Var FirefoxLaunchCode

; The first three tick counts are for the start of a phase and equate equate to
; the display of individual installer pages.
Var StartIntroPhaseTickCount
Var StartOptionsPhaseTickCount
Var StartDownloadPhaseTickCount
; Since the Intro and Options pages can be displayed multiple times the total
; seconds spent on each of these pages is reported.
Var IntroPhaseSeconds
Var OptionsPhaseSeconds
; The tick count for the last download.
Var StartLastDownloadTickCount
; The number of seconds from the start of the download phase until the first
; bytes are received. This is only recorded for first request so it is possible
; to determine connection issues for the first request.
Var DownloadFirstTransferSeconds
; The last four tick counts are for the end of a phase in the installation page.
Var EndDownloadPhaseTickCount
Var EndPreInstallPhaseTickCount
Var EndInstallPhaseTickCount
Var EndFinishPhaseTickCount

Var InitialInstallRequirementsCode
Var ExistingProfile
Var ExistingVersion
Var ExistingBuildID
Var DownloadedBytes
Var DownloadRetryCount
Var OpenedDownloadPage
Var DownloadServerIP
Var PostSigningData
Var PreviousInstallDir
Var PreviousInstallArch

Var ControlHeightPX
Var ControlRightPX
Var ControlTopAdjustment
Var OptionsItemWidthPX

; Uncomment the following to prevent pinging the metrics server when testing
; the stub installer
;!define STUB_DEBUG

!define StubURLVersion "v7"

; Successful install exit code
!define ERR_SUCCESS 0

/**
 * The following errors prefixed with ERR_DOWNLOAD apply to the download phase.
 */
; The download was cancelled by the user
!define ERR_DOWNLOAD_CANCEL 10

; Too many attempts to download. The maximum attempts is defined in
; DownloadMaxRetries.
!define ERR_DOWNLOAD_TOO_MANY_RETRIES 11

/**
 * The following errors prefixed with ERR_PREINSTALL apply to the pre-install
 * check phase.
 */
; Unable to acquire a file handle to the downloaded file
!define ERR_PREINSTALL_INVALID_HANDLE 20

; The downloaded file's certificate is not trusted by the certificate store.
!define ERR_PREINSTALL_CERT_UNTRUSTED 21

; The downloaded file's certificate attribute values were incorrect.
!define ERR_PREINSTALL_CERT_ATTRIBUTES 22

; The downloaded file's certificate is not trusted by the certificate store and
; certificate attribute values were incorrect.
!define ERR_PREINSTALL_CERT_UNTRUSTED_AND_ATTRIBUTES 23

/**
 * The following errors prefixed with ERR_INSTALL apply to the install phase.
 */
; The installation timed out. The installation timeout is defined by the number
; of progress steps defined in InstallTotalSteps and the install timer
; interval defined in InstallIntervalMS
!define ERR_INSTALL_TIMEOUT 30

; Maximum times to retry the download before displaying an error
!define DownloadMaxRetries 9

; Minimum size expected to download in bytes
!define DownloadMinSizeBytes 15728640 ; 15 MB

; Maximum size expected to download in bytes
!define DownloadMaxSizeBytes 73400320 ; 70 MB

; Interval before retrying to download. 3 seconds is used along with 10
; attempted downloads (the first attempt along with 9 retries) to give a
; minimum of 30 seconds or retrying before giving up.
!define DownloadRetryIntervalMS 3000

; Interval for the download timer
!define DownloadIntervalMS 200

; Interval for the install timer
!define InstallIntervalMS 100

; The first step for the install progress bar. By starting with a large step
; immediate feedback is given to the user.
!define InstallProgressFirstStep 20

; The finish step size to quickly increment the progress bar after the
; installation has finished.
!define InstallProgressFinishStep 40

; Number of steps for the install progress.
; This might not be enough when installing on a slow network drive so it will
; fallback to downloading the full installer if it reaches this number. The size
; of the install progress step is increased when the full installer finishes
; instead of waiting.

; Approximately 150 seconds with a 100 millisecond timer and a first step of 20
; as defined by InstallProgressFirstStep.
!define /math InstallCleanTotalSteps ${InstallProgressFirstStep} + 1500

; Approximately 165 seconds (minus 0.2 seconds for each file that is removed)
; with a 100 millisecond timer and a first step of 20 as defined by
; InstallProgressFirstStep .
!define /math InstallPaveOverTotalSteps ${InstallProgressFirstStep} + 1800

; Attempt to elevate Standard Users in addition to users that
; are a member of the Administrators group.
!define NONADMIN_ELEVATE

!define CONFIG_INI "config.ini"

!ifndef FILE_SHARE_READ
  !define FILE_SHARE_READ 1
!endif
!ifndef GENERIC_READ
  !define GENERIC_READ 0x80000000
!endif
!ifndef OPEN_EXISTING
  !define OPEN_EXISTING 3
!endif
!ifndef INVALID_HANDLE_VALUE
  !define INVALID_HANDLE_VALUE -1
!endif

!define DefaultInstDir32bit "$PROGRAMFILES32\${BrandFullName}"
!define DefaultInstDir64bit "$PROGRAMFILES64\${BrandFullName}"

!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "TextFunc.nsh"
!include "WinVer.nsh"
!include "WordFunc.nsh"

!insertmacro GetParameters
!insertmacro GetOptions
!insertmacro LineFind
!insertmacro StrFilter

!include "StrFunc.nsh"
${StrTok}

!include "locales.nsi"
!include "branding.nsi"

!include "defines.nsi"

; Must be included after defines.nsi
!include "locale-fonts.nsh"

; The OFFICIAL define is a workaround to support different urls for Release and
; Beta since they share the same branding when building with other branches that
; set the update channel to beta.
!ifdef OFFICIAL
!ifdef BETA_UPDATE_CHANNEL
!undef URLStubDownload32
!undef URLStubDownload64
!define URLStubDownload32 "http://download.mozilla.org/?os=win&lang=${AB_CD}&product=firefox-beta-latest"
!define URLStubDownload64 "http://download.mozilla.org/?os=win64&lang=${AB_CD}&product=firefox-beta-latest"
!undef URLManualDownload
!define URLManualDownload "https://www.mozilla.org/${AB_CD}/firefox/installer-help/?channel=beta&installer_lang=${AB_CD}"
!undef Channel
!define Channel "beta"
!endif
!endif

!include "common.nsh"

!insertmacro ElevateUAC
!insertmacro GetLongPath
!insertmacro GetPathFromString
!insertmacro GetParent
!insertmacro GetSingleInstallPath
!insertmacro GetTextWidthHeight
!insertmacro IsUserAdmin
!insertmacro RemovePrecompleteEntries
!insertmacro SetBrandNameVars
!insertmacro ITBL3Create
!insertmacro UnloadUAC

VIAddVersionKey "FileDescription" "${BrandShortName} Stub Installer"
VIAddVersionKey "OriginalFilename" "setup-stub.exe"

Name "$BrandFullName"
OutFile "setup-stub.exe"
icon "setup.ico"
XPStyle on
BrandingText " "
ChangeUI all "nsisui.exe"

!ifdef ${AB_CD}_rtl
  LoadLanguageFile "locale-rtl.nlf"
!else
  LoadLanguageFile "locale.nlf"
!endif

!include "nsisstrings.nlf"

!if "${AB_CD}" == "en-US"
  ; Custom strings for en-US. This is done here so they aren't translated.
  !include oneoff_en-US.nsh
!else
  !define INTRO_BLURB "$(INTRO_BLURB1)"
  !define INSTALL_BLURB1 "$(INSTALL_BLURB1)"
  !define INSTALL_BLURB2 "$(INSTALL_BLURB2)"
  !define INSTALL_BLURB3 "$(INSTALL_BLURB3)"
!endif

Caption "$(WIN_CAPTION)"

Page custom createDummy ; Needed to enable the Intro page's back button
Page custom createIntro leaveIntro ; Introduction page
Page custom createOptions leaveOptions ; Options page
Page custom createInstall ; Download / Installation page

Function .onInit
  ; Remove the current exe directory from the search order.
  ; This only effects LoadLibrary calls and not implicitly loaded DLLs.
  System::Call 'kernel32::SetDllDirectoryW(w "")'

  StrCpy $LANGUAGE 0
  ; This macro is used to set the brand name variables but the ini file method
  ; isn't supported for the stub installer.
  ${SetBrandNameVars} "$PLUGINSDIR\ignored.ini"

  ; Don't install on systems that don't support SSE2. The parameter value of
  ; 10 is for PF_XMMI64_INSTRUCTIONS_AVAILABLE which will check whether the
  ; SSE2 instruction set is available.
  System::Call "kernel32::IsProcessorFeaturePresent(i 10)i .R7"

  ; Windows NT 6.0 (Vista/Server 2008) and lower are not supported.
  ${Unless} ${AtLeastWin7}
    ${If} "$R7" == "0"
      strCpy $R7 "$(WARN_MIN_SUPPORTED_OSVER_CPU_MSG)"
    ${Else}
      strCpy $R7 "$(WARN_MIN_SUPPORTED_OSVER_MSG)"
    ${EndIf}
    MessageBox MB_OKCANCEL|MB_ICONSTOP "$R7" IDCANCEL +2
    ExecShell "open" "${URLSystemRequirements}"
    Quit
  ${EndUnless}

  ; SSE2 CPU support
  ${If} "$R7" == "0"
    MessageBox MB_OKCANCEL|MB_ICONSTOP "$(WARN_MIN_SUPPORTED_CPU_MSG)" IDCANCEL +2
    ExecShell "open" "${URLSystemRequirements}"
    Quit
  ${EndIf}

  ${If} ${RunningX64}
    StrCpy $INSTDIR "${DefaultInstDir64bit}"
  ${Else}
    StrCpy $INSTDIR "${DefaultInstDir32bit}"
  ${EndIf}

  ; Require elevation if the user can elevate
  ${ElevateUAC}

  ; If we have any existing installation, use its location as the default
  ; path for this install, even if it's not the same architecture.
  SetRegView 32
  SetShellVarContext all ; Set SHCTX to HKLM
  ${GetSingleInstallPath} "Software\Mozilla\${BrandFullNameInternal}" $R9

  ${If} "$R9" == "false"
  ${AndIf} ${RunningX64}
    SetRegView 64
    ${GetSingleInstallPath} "Software\Mozilla\${BrandFullNameInternal}" $R9
  ${EndIf}

  ${If} "$R9" == "false"
    SetShellVarContext current ; Set SHCTX to HKCU
    ${GetSingleInstallPath} "Software\Mozilla\${BrandFullNameInternal}" $R9

    ${If} ${RunningX64}
      ; In HKCU there is no WOW64 redirection, which means we may have gotten
      ; the path to a 32-bit install even though we're 64-bit.
      ; In that case, just use the default path instead of offering an upgrade.
      ; But only do that override if the existing install is in Program Files,
      ; because that's the only place we can be sure is specific
      ; to either 32 or 64 bit applications.
      ; The WordFind syntax below searches for the first occurence of the
      ; "delimiter" (the Program Files path) in the install path and returns
      ; anything that appears before that. If nothing appears before that,
      ; then the install is under Program Files.
      ${WordFind} $R9 $PROGRAMFILES32 "+1{" $0
      ${If} $0 == ""
        StrCpy $R9 "false"
      ${EndIf}
    ${EndIf}
  ${EndIf}

  StrCpy $PreviousInstallDir ""
  StrCpy $PreviousInstallArch ""
  ${If} "$R9" != "false"
    ; Don't override the default install path with an existing installation
    ; of a different architecture.
    System::Call "*(i)p.r0"
    StrCpy $1 "$R9\${FileMainEXE}"
    System::Call "Kernel32::GetBinaryTypeW(w r1, p r0)i"
    System::Call "*$0(i.r2)"
    System::Free $0

    ${If} $2 == "6" ; 6 == SCS_64BIT_BINARY
    ${AndIf} ${RunningX64}
      StrCpy $PreviousInstallDir "$R9"
      StrCpy $PreviousInstallArch "64"
      StrCpy $INSTDIR "$PreviousInstallDir"
    ${ElseIf} $2 == "0" ; 0 == SCS_32BIT_BINARY
    ${AndIfNot} ${RunningX64}
      StrCpy $PreviousInstallDir "$R9"
      StrCpy $PreviousInstallArch "32"
      StrCpy $INSTDIR "$PreviousInstallDir"
    ${EndIf}
  ${EndIf}

  ; Used to determine if the default installation directory was used.
  StrCpy $InitialInstallDir "$INSTDIR"

  ClearErrors
  WriteRegStr HKLM "Software\Mozilla" "${BrandShortName}InstallerTest" \
                   "Write Test"

  ; Only display set as default when there is write access to HKLM and on Win7
  ; and below.
  ${If} ${Errors}
  ${OrIf} ${AtLeastWin8}
    StrCpy $CanSetAsDefault "false"
    StrCpy $CheckboxSetAsDefault "0"
  ${Else}
    DeleteRegValue HKLM "Software\Mozilla" "${BrandShortName}InstallerTest"
    StrCpy $CanSetAsDefault "true"
  ${EndIf}

  ; The interval in MS used for the progress bars set as marquee.
  StrCpy $ProgressbarMarqueeIntervalMS "10"

  ; Initialize the majority of variables except those that need to be reset
  ; when a page is displayed.
  StrCpy $IntroPhaseSeconds "0"
  StrCpy $OptionsPhaseSeconds "0"
  StrCpy $EndPreInstallPhaseTickCount "0"
  StrCpy $EndInstallPhaseTickCount "0"
  StrCpy $InitialInstallRequirementsCode ""
  StrCpy $IsDownloadFinished ""
  StrCpy $FirefoxLaunchCode "0"
  StrCpy $CheckboxShortcuts "1"
  StrCpy $CheckboxSendPing "1"
!ifdef MOZ_MAINTENANCE_SERVICE
  StrCpy $CheckboxInstallMaintSvc "1"
!else
  StrCpy $CheckboxInstallMaintSvc "0"
!endif
  StrCpy $WasOptionsButtonClicked "0"
  ${If} ${RunningX64}
    StrCpy $DroplistArch "$(VERSION_64BIT)"
  ${Else}
    StrCpy $DroplistArch "$(VERSION_32BIT)"
  ${EndIf}

  StrCpy $0 ""
!ifdef FONT_FILE1
  ${If} ${FileExists} "$FONTS\${FONT_FILE1}"
    StrCpy $0 "${FONT_NAME1}"
  ${EndIf}
!endif

!ifdef FONT_FILE2
  ${If} $0 == ""
  ${AndIf} ${FileExists} "$FONTS\${FONT_FILE2}"
    StrCpy $0 "${FONT_NAME2}"
  ${EndIf}
!endif

  ${If} $0 == ""
    StrCpy $0 "$(^Font)"
  ${EndIf}

  CreateFont $FontBlurb "$0" "12" "500"
  CreateFont $FontNormal "$0" "11" "500"
  CreateFont $FontItalic "$0" "11" "500" /ITALIC

  InitPluginsDir
  File /oname=$PLUGINSDIR\bgintro.bmp "bgintro.bmp"
  File /oname=$PLUGINSDIR\appname.bmp "appname.bmp"
  File /oname=$PLUGINSDIR\clock.bmp "clock.bmp"
  File /oname=$PLUGINSDIR\particles.bmp "particles.bmp"
!ifdef ${AB_CD}_rtl
  ; The horizontally flipped pencil looks better in RTL
  File /oname=$PLUGINSDIR\pencil.bmp "pencil-rtl.bmp"
!else
  File /oname=$PLUGINSDIR\pencil.bmp "pencil.bmp"
!endif
FunctionEnd

; .onGUIInit isn't needed except for RTL locales
!ifdef ${AB_CD}_rtl
Function .onGUIInit
  ; Since NSIS RTL support doesn't mirror progress bars use Windows mirroring.
  ${NSD_AddExStyle} $HWNDPARENT ${WS_EX_LAYOUTRTL}
  ${RemoveExStyle} $HWNDPARENT ${WS_EX_RTLREADING}
  ${RemoveExStyle} $HWNDPARENT ${WS_EX_RIGHT}
  ${NSD_AddExStyle} $HWNDPARENT ${WS_EX_LEFT}|${WS_EX_LTRREADING}
FunctionEnd
!endif

Function .onGUIEnd
  Delete "$PLUGINSDIR\_temp"
  Delete "$PLUGINSDIR\download.exe"
  Delete "$PLUGINSDIR\${CONFIG_INI}"

  ${UnloadUAC}
FunctionEnd

Function .onUserAbort
  ${NSD_KillTimer} StartDownload
  ${NSD_KillTimer} OnDownload
  ${NSD_KillTimer} CheckInstall
  ${NSD_KillTimer} FinishInstall
  ${NSD_KillTimer} FinishProgressBar
  ${NSD_KillTimer} DisplayDownloadError

  ${If} "$IsDownloadFinished" != ""
    Call DisplayDownloadError
    ; Aborting the abort will allow SendPing which is called by
    ; DisplayDownloadError to hide the installer window and close the installer
    ; after it sends the metrics ping.
    Abort
  ${EndIf}
FunctionEnd

Function SendPing
  HideWindow
  ; Try to send a ping if a download was attempted
  ${If} $CheckboxSendPing == 1
  ${AndIf} $IsDownloadFinished != ""
    ; Get the tick count for the completion of all phases.
    System::Call "kernel32::GetTickCount()l .s"
    Pop $EndFinishPhaseTickCount

    ; When the value of $IsDownloadFinished is false the download was started
    ; but didn't finish. In this case the tick count stored in
    ; $EndFinishPhaseTickCount is used to determine how long the download was
    ; in progress.
    ${If} "$IsDownloadFinished" == "false"
    ${OrIf} "$EndDownloadPhaseTickCount" == ""
      StrCpy $EndDownloadPhaseTickCount "$EndFinishPhaseTickCount"
      ; Cancel the download in progress
      InetBgDL::Get /RESET /END
    ${EndIf}


    ; When $DownloadFirstTransferSeconds equals an empty string the download
    ; never successfully started so set the value to 0. It will be possible to
    ; determine that the download didn't successfully start from the seconds for
    ; the last download.
    ${If} "$DownloadFirstTransferSeconds" == ""
      StrCpy $DownloadFirstTransferSeconds "0"
    ${EndIf}

    ; When $StartLastDownloadTickCount equals an empty string the download never
    ; successfully started so set the value to $EndDownloadPhaseTickCount to
    ; compute the correct value.
    ${If} $StartLastDownloadTickCount == ""
      ; This could happen if the download never successfully starts
      StrCpy $StartLastDownloadTickCount "$EndDownloadPhaseTickCount"
    ${EndIf}

    ; When $EndPreInstallPhaseTickCount equals 0 the installation phase was
    ; never completed so set its value to $EndFinishPhaseTickCount to compute
    ; the correct value.
    ${If} "$EndPreInstallPhaseTickCount" == "0"
      StrCpy $EndPreInstallPhaseTickCount "$EndFinishPhaseTickCount"
    ${EndIf}

    ; When $EndInstallPhaseTickCount equals 0 the installation phase was never
    ; completed so set its value to $EndFinishPhaseTickCount to compute the
    ; correct value.
    ${If} "$EndInstallPhaseTickCount" == "0"
      StrCpy $EndInstallPhaseTickCount "$EndFinishPhaseTickCount"
    ${EndIf}

    ; Get the seconds elapsed from the start of the download phase to the end of
    ; the download phase.
    ${GetSecondsElapsed} "$StartDownloadPhaseTickCount" "$EndDownloadPhaseTickCount" $0

    ; Get the seconds elapsed from the start of the last download to the end of
    ; the last download.
    ${GetSecondsElapsed} "$StartLastDownloadTickCount" "$EndDownloadPhaseTickCount" $1

    ; Get the seconds elapsed from the end of the download phase to the
    ; completion of the pre-installation check phase.
    ${GetSecondsElapsed} "$EndDownloadPhaseTickCount" "$EndPreInstallPhaseTickCount" $2

    ; Get the seconds elapsed from the end of the pre-installation check phase
    ; to the completion of the installation phase.
    ${GetSecondsElapsed} "$EndPreInstallPhaseTickCount" "$EndInstallPhaseTickCount" $3

    ; Get the seconds elapsed from the end of the installation phase to the
    ; completion of all phases.
    ${GetSecondsElapsed} "$EndInstallPhaseTickCount" "$EndFinishPhaseTickCount" $4

    ${If} $DroplistArch == "$(VERSION_64BIT)"
      StrCpy $R0 "1"
    ${Else}
      StrCpy $R0 "0"
    ${EndIf}

    ${If} ${RunningX64}
      StrCpy $R1 "1"
    ${Else}
      StrCpy $R1 "0"
    ${EndIf}

    ; Though these values are sometimes incorrect due to bug 444664 it happens
    ; so rarely it isn't worth working around it by reading the registry values.
    ${WinVerGetMajor} $5
    ${WinVerGetMinor} $6
    ${WinVerGetBuild} $7
    ${WinVerGetServicePackLevel} $8
    ${If} ${IsServerOS}
      StrCpy $9 "1"
    ${Else}
      StrCpy $9 "0"
    ${EndIf}

    ${If} "$ExitCode" == "${ERR_SUCCESS}"
      ReadINIStr $R5 "$INSTDIR\application.ini" "App" "Version"
      ReadINIStr $R6 "$INSTDIR\application.ini" "App" "BuildID"
    ${Else}
      StrCpy $R5 "0"
      StrCpy $R6 "0"
    ${EndIf}

    ; Whether installed into the default installation directory
    ${GetLongPath} "$INSTDIR" $R7
    ${GetLongPath} "$InitialInstallDir" $R8
    ${If} "$R7" == "$R8"
      StrCpy $R7 "1"
    ${Else}
      StrCpy $R7 "0"
    ${EndIf}

    ClearErrors
    WriteRegStr HKLM "Software\Mozilla" "${BrandShortName}InstallerTest" \
                     "Write Test"
    ${If} ${Errors}
      StrCpy $R8 "0"
    ${Else}
      DeleteRegValue HKLM "Software\Mozilla" "${BrandShortName}InstallerTest"
      StrCpy $R8 "1"
    ${EndIf}

    ${If} "$DownloadServerIP" == ""
      StrCpy $DownloadServerIP "Unknown"
    ${EndIf}

    StrCpy $R2 ""
    SetShellVarContext current ; Set SHCTX to the current user
    ReadRegStr $R2 HKCU "Software\Classes\http\shell\open\command" ""
    ${If} $R2 != ""
      ${GetPathFromString} "$R2" $R2
      ${GetParent} "$R2" $R3
      ${GetLongPath} "$R3" $R3
      ${If} $R3 == $INSTDIR
        StrCpy $R2 "1" ; This Firefox install is set as default.
      ${Else}
        StrCpy $R2 "$R2" "" -11 # length of firefox.exe
        ${If} "$R2" == "${FileMainEXE}"
          StrCpy $R2 "2" ; Another Firefox install is set as default.
        ${Else}
          StrCpy $R2 "0"
        ${EndIf}
      ${EndIf}
    ${Else}
      StrCpy $R2 "0" ; Firefox is not set as default.
    ${EndIf}

    ${If} "$R2" == "0"
      StrCpy $R3 ""
      ReadRegStr $R2 HKLM "Software\Classes\http\shell\open\command" ""
      ${If} $R2 != ""
        ${GetPathFromString} "$R2" $R2
        ${GetParent} "$R2" $R3
        ${GetLongPath} "$R3" $R3
        ${If} $R3 == $INSTDIR
          StrCpy $R2 "1" ; This Firefox install is set as default.
        ${Else}
          StrCpy $R2 "$R2" "" -11 # length of firefox.exe
          ${If} "$R2" == "${FileMainEXE}"
            StrCpy $R2 "2" ; Another Firefox install is set as default.
          ${Else}
            StrCpy $R2 "0"
          ${EndIf}
        ${EndIf}
      ${Else}
        StrCpy $R2 "0" ; Firefox is not set as default.
      ${EndIf}
    ${EndIf}

    ${If} $CanSetAsDefault == "true"
      ${If} $CheckboxSetAsDefault == "1"
        StrCpy $R3 "2"
      ${Else}
        StrCpy $R3 "3"
      ${EndIf}
    ${Else}
      ${If} ${AtLeastWin8}
        StrCpy $R3 "1"
      ${Else}
        StrCpy $R3 "0"
      ${EndIf}
    ${EndIf}

!ifdef STUB_DEBUG
    MessageBox MB_OK "${BaseURLStubPing} \
                      $\nStub URL Version = ${StubURLVersion}${StubURLVersionAppend} \
                      $\nBuild Channel = ${Channel} \
                      $\nUpdate Channel = ${UpdateChannel} \
                      $\nLocale = ${AB_CD} \
                      $\nFirefox x64 = $R0 \
                      $\nRunning x64 Windows = $R1 \
                      $\nMajor = $5 \
                      $\nMinor = $6 \
                      $\nBuild = $7 \
                      $\nServicePack = $8 \
                      $\nIsServer = $9 \
                      $\nExit Code = $ExitCode \
                      $\nFirefox Launch Code = $FirefoxLaunchCode \
                      $\nDownload Retry Count = $DownloadRetryCount \
                      $\nDownloaded Bytes = $DownloadedBytes \
                      $\nDownload Size Bytes = $DownloadSizeBytes \
                      $\nIntroduction Phase Seconds = $IntroPhaseSeconds \
                      $\nOptions Phase Seconds = $OptionsPhaseSeconds \
                      $\nDownload Phase Seconds = $0 \
                      $\nLast Download Seconds = $1 \
                      $\nDownload First Transfer Seconds = $DownloadFirstTransferSeconds \
                      $\nPreinstall Phase Seconds = $2 \
                      $\nInstall Phase Seconds = $3 \
                      $\nFinish Phase Seconds = $4 \
                      $\nInitial Install Requirements Code = $InitialInstallRequirementsCode \
                      $\nOpened Download Page = $OpenedDownloadPage \
                      $\nExisting Profile = $ExistingProfile \
                      $\nExisting Version = $ExistingVersion \
                      $\nExisting Build ID = $ExistingBuildID \
                      $\nNew Version = $R5 \
                      $\nNew Build ID = $R6 \
                      $\nDefault Install Dir = $R7 \
                      $\nHas Admin = $R8 \
                      $\nDefault Status = $R2 \
                      $\nSet As Sefault Status = $R3 \
                      $\nDownload Server IP = $DownloadServerIP \
                      $\nPost-Signing Data = $PostSigningData"
    ; The following will exit the installer
    SetAutoClose true
    StrCpy $R9 "2"
    Call RelativeGotoPage
!else
    ${NSD_CreateTimer} OnPing ${DownloadIntervalMS}
    InetBgDL::Get "${BaseURLStubPing}/${StubURLVersion}${StubURLVersionAppend}/${Channel}/${UpdateChannel}/${AB_CD}/$R0/$R1/$5/$6/$7/$8/$9/$ExitCode/$FirefoxLaunchCode/$DownloadRetryCount/$DownloadedBytes/$DownloadSizeBytes/$IntroPhaseSeconds/$OptionsPhaseSeconds/$0/$1/$DownloadFirstTransferSeconds/$2/$3/$4/$InitialInstallRequirementsCode/$OpenedDownloadPage/$ExistingProfile/$ExistingVersion/$ExistingBuildID/$R5/$R6/$R7/$R8/$R2/$R3/$DownloadServerIP/$PostSigningData" \
                  "$PLUGINSDIR\_temp" /END
!endif
  ${Else}
    ${If} "$IsDownloadFinished" == "false"
      ; Cancel the download in progress
      InetBgDL::Get /RESET /END
    ${EndIf}
    ; The following will exit the installer
    SetAutoClose true
    StrCpy $R9 "2"
    Call RelativeGotoPage
  ${EndIf}
FunctionEnd

Function createDummy
FunctionEnd

Function createIntro
  nsDialogs::Create /NOUNLOAD 1018
  Pop $Dialog

  GetFunctionAddress $0 OnBack
  nsDialogs::OnBack /NOUNLOAD $0

!ifdef ${AB_CD}_rtl
  ; For RTL align the text with the top of the F in the Firefox bitmap
  StrCpy $0 "${INTRO_BLURB_RTL_TOP_DU}"
!else
  ; For LTR align the text with the top of the x in the Firefox bitmap
  StrCpy $0 "${INTRO_BLURB_LTR_TOP_DU}"
!endif
  ${NSD_CreateLabel} ${INTRO_BLURB_EDGE_DU} $0 ${INTRO_BLURB_WIDTH_DU} 76u "${INTRO_BLURB}"
  Pop $0
  SendMessage $0 ${WM_SETFONT} $FontBlurb 0
  SetCtlColors $0 ${INTRO_BLURB_TEXT_COLOR} transparent

  SetCtlColors $HWNDPARENT ${FOOTER_CONTROL_TEXT_COLOR_NORMAL} ${FOOTER_BKGRD_COLOR}
  GetDlgItem $0 $HWNDPARENT 10 ; Default browser checkbox
  ${If} "$CanSetAsDefault" == "true"
    ; The uxtheme must be disabled on checkboxes in order to override the
    ; system font color.
    System::Call 'uxtheme::SetWindowTheme(i $0 , w " ", w " ")'
    SendMessage $0 ${WM_SETFONT} $FontNormal 0
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(MAKE_DEFAULT)"
    SendMessage $0 ${BM_SETCHECK} 1 0
    SetCtlColors $0 ${FOOTER_CONTROL_TEXT_COLOR_NORMAL} ${FOOTER_BKGRD_COLOR}
  ${Else}
    ShowWindow $0 ${SW_HIDE}
  ${EndIf}
  GetDlgItem $0 $HWNDPARENT 11
  ShowWindow $0 ${SW_HIDE}

  ${NSD_CreateBitmap} ${APPNAME_BMP_EDGE_DU} ${APPNAME_BMP_TOP_DU} \
                      ${APPNAME_BMP_WIDTH_DU} ${APPNAME_BMP_HEIGHT_DU} ""
  Pop $2
  ${SetStretchedTransparentImage} $2 $PLUGINSDIR\appname.bmp $0

  ${NSD_CreateBitmap} 0 0 100% 100% ""
  Pop $2
  ${NSD_SetStretchedImage} $2 $PLUGINSDIR\bgintro.bmp $1

  GetDlgItem $0 $HWNDPARENT 1 ; Install button
  ${If} ${FileExists} "$INSTDIR\${FileMainEXE}"
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(UPGRADE_BUTTON)"
  ${Else}
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(INSTALL_BUTTON)"
  ${EndIf}
  ${NSD_SetFocus} $0

  GetDlgItem $0 $HWNDPARENT 2 ; Cancel button
  SendMessage $0 ${WM_SETTEXT} 0 "STR:$(CANCEL_BUTTON)"

  GetDlgItem $0 $HWNDPARENT 3 ; Back button used for Options
  SendMessage $0 ${WM_SETTEXT} 0 "STR:$(OPTIONS_BUTTON)"

  System::Call "kernel32::GetTickCount()l .s"
  Pop $StartIntroPhaseTickCount

  LockWindow off
  nsDialogs::Show

  ${NSD_FreeImage} $0
  ${NSD_FreeImage} $1
FunctionEnd

Function leaveIntro
  LockWindow on

  System::Call "kernel32::GetTickCount()l .s"
  Pop $0
  ${GetSecondsElapsed} "$StartIntroPhaseTickCount" "$0" $IntroPhaseSeconds
  ; It is possible for this value to be 0 if the user clicks fast enough so
  ; increment the value by 1 if it is 0.
  ${If} $IntroPhaseSeconds == 0
    IntOp $IntroPhaseSeconds $IntroPhaseSeconds + 1
  ${EndIf}

  SetShellVarContext all ; Set SHCTX to All Users
  ; If the user doesn't have write access to the installation directory set
  ; the installation directory to a subdirectory of the All Users application
  ; directory and if the user can't write to that location set the installation
  ; directory to a subdirectory of the users local application directory
  ; (e.g. non-roaming).
  Call CanWrite
  ${If} "$CanWriteToInstallDir" == "false"
    StrCpy $INSTDIR "$APPDATA\${BrandFullName}\"
    Call CanWrite
    ${If} "$CanWriteToInstallDir" == "false"
      ; This should never happen but just in case.
      StrCpy $CanWriteToInstallDir "false"
    ${Else}
      StrCpy $INSTDIR "$LOCALAPPDATA\${BrandFullName}\"
      Call CanWrite
    ${EndIf}
  ${EndIf}

  Call CheckSpace

  ${If} ${FileExists} "$INSTDIR"
    ; Always display the long path if the path exists.
    ${GetLongPath} "$INSTDIR" $INSTDIR
  ${EndIf}

FunctionEnd

Function createOptions
  ; Check whether the install requirements are satisfied using the default
  ; values for metrics.
  ${If} "$InitialInstallRequirementsCode" == ""
    ${If} "$CanWriteToInstallDir" != "true"
    ${AndIf} "$HasRequiredSpaceAvailable" != "true"
      StrCpy $InitialInstallRequirementsCode "1"
    ${ElseIf} "$CanWriteToInstallDir" != "true"
      StrCpy $InitialInstallRequirementsCode "2"
    ${ElseIf} "$HasRequiredSpaceAvailable" != "true"
      StrCpy $InitialInstallRequirementsCode "3"
    ${Else}
      StrCpy $InitialInstallRequirementsCode "0"
    ${EndIf}
  ${EndIf}

  ; Skip the options page unless the Options button was clicked as long as the
  ; installation directory can be written to and there is the minimum required
  ; space available.
  ${If} "$WasOptionsButtonClicked" != "1"
    ${If} "$CanWriteToInstallDir" == "true"
    ${AndIf} "$HasRequiredSpaceAvailable" == "true"
      Abort ; Skip the options page
    ${EndIf}
  ${EndIf}

  StrCpy $ExistingTopDir ""
  StrCpy $ControlTopAdjustment 0

  ; Convert the options item width to pixels, so we can tell when a text string
  ; exceeds this width and needs multiple lines.
  StrCpy $2 "${OPTIONS_ITEM_WIDTH_DU}" -1
  IntOp $2 $2 - 14 ; subtract approximate width of a checkbox
  System::Call "*(i r2,i,i,i) p .r3"
  System::Call "user32::MapDialogRect(p $HWNDPARENT, p r3)"
  System::Call "*$3(i .s,i,i,i)"
  Pop $OptionsItemWidthPX
  System::Free $3

  nsDialogs::Create /NOUNLOAD 1018
  Pop $Dialog
  ; Since the text color for controls is set in this Dialog the foreground and
  ; background colors of the Dialog must also be hardcoded.
  SetCtlColors $Dialog ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}

  ${NSD_CreateLabel} ${OPTIONS_ITEM_EDGE_DU} 25u ${OPTIONS_ITEM_WIDTH_DU} \
                     12u "$(DEST_FOLDER)"
  Pop $0
  SetCtlColors $0 ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}
  SendMessage $0 ${WM_SETFONT} $FontNormal 0

  ${NSD_CreateDirRequest} ${OPTIONS_SUBITEM_EDGE_DU} 41u 159u 14u "$INSTDIR"
  Pop $DirRequest
  SetCtlColors $DirRequest ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}
  SendMessage $DirRequest ${WM_SETFONT} $FontNormal 0
  System::Call shlwapi::SHAutoComplete(i $DirRequest, i ${SHACF_FILESYSTEM})
  ${NSD_OnChange} $DirRequest OnChange_DirRequest

!ifdef ${AB_CD}_rtl
  ; Remove the RTL styling from the directory request text box
  ${RemoveStyle} $DirRequest ${SS_RIGHT}
  ${RemoveExStyle} $DirRequest ${WS_EX_RIGHT}
  ${RemoveExStyle} $DirRequest ${WS_EX_RTLREADING}
  ${NSD_AddStyle} $DirRequest ${SS_LEFT}
  ${NSD_AddExStyle} $DirRequest ${WS_EX_LTRREADING}|${WS_EX_LEFT}
!endif

  ${NSD_CreateBrowseButton} 280u 41u 50u 14u "$(BROWSE_BUTTON)"
  Pop $ButtonBrowse
  SetCtlColors $ButtonBrowse "" ${COMMON_BKGRD_COLOR}
  ${NSD_OnClick} $ButtonBrowse OnClick_ButtonBrowse

  ; Get the number of pixels from the left of the Dialog to the right side of
  ; the "Space Required:" and "Space Available:" labels prior to setting RTL so
  ; the correct position of the controls can be set by NSIS for RTL locales.

  ; Get the width and height of both labels and use the tallest for the height
  ; and the widest to calculate where to place the labels after these labels.
  ${GetTextExtent} "$(SPACE_REQUIRED)" $FontItalic $0 $1
  ${GetTextExtent} "$(SPACE_AVAILABLE)" $FontItalic $2 $3
  ${If} $1 > $3
    StrCpy $ControlHeightPX "$1"
  ${Else}
    StrCpy $ControlHeightPX "$3"
  ${EndIf}

  IntOp $0 $0 + 8 ; Add padding to the control's width
  ; Make both controls the same width as the widest control
  ${NSD_CreateLabelCenter} ${OPTIONS_SUBITEM_EDGE_DU} 59u $0 $ControlHeightPX "$(SPACE_REQUIRED)"
  Pop $5
  SetCtlColors $5 ${COMMON_TEXT_COLOR_FADED} ${COMMON_BKGRD_COLOR}
  SendMessage $5 ${WM_SETFONT} $FontItalic 0

  IntOp $2 $2 + 8 ; Add padding to the control's width
  ${NSD_CreateLabelCenter} ${OPTIONS_SUBITEM_EDGE_DU} 70u $2 $ControlHeightPX "$(SPACE_AVAILABLE)"
  Pop $6
  SetCtlColors $6 ${COMMON_TEXT_COLOR_FADED} ${COMMON_BKGRD_COLOR}
  SendMessage $6 ${WM_SETFONT} $FontItalic 0

  ; Use the widest label for aligning the labels next to them
  ${If} $0 > $2
    StrCpy $6 "$5"
  ${EndIf}
  FindWindow $1 "#32770" "" $HWNDPARENT
  ${GetDlgItemEndPX} $6 $ControlRightPX

  IntOp $ControlRightPX $ControlRightPX + 6

  ${NSD_CreateLabel} $ControlRightPX 59u 100% $ControlHeightPX \
                     "${APPROXIMATE_REQUIRED_SPACE_MB} $(MEGA)$(BYTE)"
  Pop $7
  SetCtlColors $7 ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}
  SendMessage $7 ${WM_SETFONT} $FontNormal 0

  ; Create the free space label with an empty string and update it by calling
  ; UpdateFreeSpaceLabel
  ${NSD_CreateLabel} $ControlRightPX 70u 100% $ControlHeightPX " "
  Pop $LabelFreeSpace
  SetCtlColors $LabelFreeSpace ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}
  SendMessage $LabelFreeSpace ${WM_SETFONT} $FontNormal 0

  Call UpdateFreeSpaceLabel

  ${If} ${AtLeastWin7}
    StrCpy $0 "$(ADD_SC_DESKTOP_TASKBAR)"
  ${Else}
    StrCpy $0 "$(ADD_SC_DESKTOP_QUICKLAUNCHBAR)"
  ${EndIf}

  ; In some locales, this string may be too long to fit on one line.
  ; In that case, we'll need to give the control two lines worth of height.
  StrCpy $1 12 ; single line height
  ${GetTextExtent} $0 $FontNormal $R1 $R2
  ${If} $R1 > $OptionsItemWidthPX
    ; Add a second line to the control height.
    IntOp $1 $1 + 12
    ; The rest of the controls will have to be lower to account for this label
    ; needing two lines worth of height.
    IntOp $ControlTopAdjustment $ControlTopAdjustment + 12
  ${EndIf}
  ${NSD_CreateCheckbox} ${OPTIONS_ITEM_EDGE_DU} 100u \
                        ${OPTIONS_ITEM_WIDTH_DU} "$1u" "$0"
  Pop $CheckboxShortcuts
  ; The uxtheme must be disabled on checkboxes in order to override the system
  ; font color.
  System::Call 'uxtheme::SetWindowTheme(i $CheckboxShortcuts, w " ", w " ")'
  SetCtlColors $CheckboxShortcuts ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}
  SendMessage $CheckboxShortcuts ${WM_SETFONT} $FontNormal 0
  ${NSD_Check} $CheckboxShortcuts

  IntOp $0 116 + $ControlTopAdjustment
  ; In some locales, this string may be too long to fit on one line.
  ; In that case, we'll need to give the control two lines worth of height.
  StrCpy $1 12 ; single line height
  ${GetTextExtent} "$(SEND_PING)" $FontNormal $R1 $R2
  ${If} $R1 > $OptionsItemWidthPX
    ; Add a second line to the control height.
    IntOp $1 $1 + 12
    ; The rest of the controls will have to be lower to account for this label
    ; needing two lines worth of height.
    IntOp $ControlTopAdjustment $ControlTopAdjustment + 12
  ${EndIf}
  ${NSD_CreateCheckbox} ${OPTIONS_ITEM_EDGE_DU} "$0u" ${OPTIONS_ITEM_WIDTH_DU} \
                        "$1u" "$(SEND_PING)"
  Pop $CheckboxSendPing
  ; The uxtheme must be disabled on checkboxes in order to override the system
  ; font color.
  System::Call 'uxtheme::SetWindowTheme(i $CheckboxSendPing, w " ", w " ")'
  SetCtlColors $CheckboxSendPing ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}
  SendMessage $CheckboxSendPing ${WM_SETFONT} $FontNormal 0
  ${NSD_Check} $CheckboxSendPing

!ifdef MOZ_MAINTENANCE_SERVICE
  StrCpy $CheckboxInstallMaintSvc "0"
  ; We can only install the maintenance service if the user is an admin.
  Call IsUserAdmin
  Pop $0

  ${If} $0 == "true"
    ; Only show the maintenance service checkbox if we have write access to HKLM
    DeleteRegValue HKLM "Software\Mozilla" "${BrandShortName}InstallerTest"
    ClearErrors
    WriteRegStr HKLM "Software\Mozilla" "${BrandShortName}InstallerTest" \
                     "Write Test"
    ${IfNot} ${Errors}
      DeleteRegValue HKLM "Software\Mozilla" "${BrandShortName}InstallerTest"
      ; Read the registry instead of using ServicesHelper::IsInstalled so the
      ; plugin isn't included in the stub installer to lessen its size.
      ClearErrors
      ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\services\MozillaMaintenance" "ImagePath"
      ${If} ${Errors}
        IntOp $0 132 + $ControlTopAdjustment
        ; In some locales, this string may be too long to fit on one line.
        ; In that case, we'll need to give the control two lines worth of height.
        StrCpy $1 12 ; single line height
        ${GetTextExtent} "$(INSTALL_MAINT_SERVICE)" $FontNormal $R1 $R2
        ${If} $R1 > $OptionsItemWidthPX
          ; Add a second line to the control height.
          IntOp $1 $1 + 12
          ; The rest of the controls will have to be lower to account for this label
          ; needing two lines worth of height.
          IntOp $ControlTopAdjustment $ControlTopAdjustment + 12
        ${EndIf}
        ${NSD_CreateCheckbox} ${OPTIONS_ITEM_EDGE_DU} "$0u" ${OPTIONS_ITEM_WIDTH_DU} \
                              "$1u" "$(INSTALL_MAINT_SERVICE)"
        Pop $CheckboxInstallMaintSvc
        System::Call 'uxtheme::SetWindowTheme(i $CheckboxInstallMaintSvc, w " ", w " ")'
        SetCtlColors $CheckboxInstallMaintSvc ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}
        SendMessage $CheckboxInstallMaintSvc ${WM_SETFONT} $FontNormal 0
        ${NSD_Check} $CheckboxInstallMaintSvc
        ; Since we're adding in an optional control, remember the lower the ones
        ; that come after it.
        IntOp $ControlTopAdjustment 20 + $ControlTopAdjustment
      ${EndIf}
    ${EndIf}
  ${EndIf}
!endif

  ${If} ${RunningX64}
    ; Get the exact pixel width we're going to need for this label.
    ; The label string has a keyboard accelerator, which is an '&' that's in
    ; the string but is not rendered, and GetTextExtent doesn't account for
    ; those, so remove them first. Also handle any escaped &'s ("&&").
    StrCpy $R0 "$(ARCH_DROPLIST_LABEL)"
    StrCpy $R1 ""
    ${Do}
      ${StrTok} $R2 $R0 "&" 0 0
      StrCpy $R1 "$R1$R2"
      StrLen $R3 $R2
      IntOp $R3 $R3 + 1
      StrCpy $R0 $R0 "" $R3
      StrCpy $R4 $R0 1
      ${If} $R4 == "&"
        StrCpy $R1 "$R1&"
        StrCpy $R0 $R0 "" 1
      ${EndIf}
    ${LoopUntil} $R0 == ""

    ${GetTextExtent} $R1 $FontNormal $R0 $R1
    IntOp $0 134 + $ControlTopAdjustment
    ${NSD_CreateLabel} ${OPTIONS_ITEM_EDGE_DU} "$0u" $R0 $R1 "$(ARCH_DROPLIST_LABEL)"
    Pop $0
    SetCtlColors $0 ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}
    SendMessage $0 ${WM_SETFONT} $FontNormal 0

    ; Set the dropdown list size to the same as the larger of the two options.
    ${GetTextExtent} "$(VERSION_32BIT)" $FontNormal $R0 $R1
    ${GetTextExtent} "$(VERSION_64BIT)" $FontNormal $R2 $R3
    ${If} $R0 < $R2
      StrCpy $R0 $R2
    ${EndIf}
    ${If} $R1 < $R3
      StrCpy $R3 $R1
    ${EndIf}
    ; Add enough width for the dropdown button. How wide the button is depends
    ; on he system display scaling setting, which we cannot easily determine,
    ; so just use a value that works fine for a setting of 200% and adds a
    ; little too much padding for settings below that.
    IntOp $R0 $R0 + 56

    ; Put the droplist right after the label, with some padding.
    ${GetDlgItemEndPX} $0 $ControlRightPX
    IntOp $ControlRightPX $ControlRightPX + 4
    IntOp $0 132 + $ControlTopAdjustment
    ${NSD_CreateDropList} $ControlRightPX "$0u" $R0 $R3 ""
    Pop $DroplistArch
    ${NSD_CB_AddString} $DroplistArch "$(VERSION_32BIT)"
    ${NSD_CB_AddString} $DroplistArch "$(VERSION_64BIT)"
    ${NSD_OnChange} $DroplistArch OnChange_DroplistArch
    ; The uxtheme must be disabled in order to override the system colors.
    System::Call 'uxtheme::SetWindowTheme(i $DroplistArch, w " ", w " ")'
    SetCtlColors $DroplistArch ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}
    SendMessage $DroplistArch ${WM_SETFONT} $FontNormal 0
    ${NSD_CB_SelectString} $DroplistArch "$(VERSION_64BIT)"
  ${EndIf}

  GetDlgItem $0 $HWNDPARENT 1 ; Install button
  ${If} ${FileExists} "$INSTDIR\${FileMainEXE}"
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(UPGRADE_BUTTON)"
  ${Else}
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(INSTALL_BUTTON)"
  ${EndIf}
  ${NSD_SetFocus} $0

  GetDlgItem $0 $HWNDPARENT 2 ; Cancel button
  SendMessage $0 ${WM_SETTEXT} 0 "STR:$(CANCEL_BUTTON)"

  GetDlgItem $0 $HWNDPARENT 3 ; Back button used for Options
  EnableWindow $0 0
  ShowWindow $0 ${SW_HIDE}

  ; If the option button was not clicked display the reason for what needs to be
  ; resolved to continue the installation.
  ${If} "$WasOptionsButtonClicked" != "1"
    ${If} "$CanWriteToInstallDir" == "false"
      MessageBox MB_OK|MB_ICONEXCLAMATION "$(WARN_WRITE_ACCESS)"
    ${ElseIf} "$HasRequiredSpaceAvailable" == "false"
      MessageBox MB_OK|MB_ICONEXCLAMATION "$(WARN_DISK_SPACE)"
    ${EndIf}
  ${EndIf}

  System::Call "kernel32::GetTickCount()l .s"
  Pop $StartOptionsPhaseTickCount

  LockWindow off
  nsDialogs::Show
FunctionEnd

Function leaveOptions
  LockWindow on

  ${GetRoot} "$INSTDIR" $0
  ${GetLongPath} "$INSTDIR" $INSTDIR
  ${GetLongPath} "$0" $0
  ${If} "$INSTDIR" == "$0"
    LockWindow off
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(WARN_ROOT_INSTALL)"
    Abort ; Stay on the page
  ${EndIf}

  Call CanWrite
  ${If} "$CanWriteToInstallDir" == "false"
    LockWindow off
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(WARN_WRITE_ACCESS)"
    Abort ; Stay on the page
  ${EndIf}

  Call CheckSpace
  ${If} "$HasRequiredSpaceAvailable" == "false"
    LockWindow off
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(WARN_DISK_SPACE)"
    Abort ; Stay on the page
  ${EndIf}

  System::Call "kernel32::GetTickCount()l .s"
  Pop $0
  ${GetSecondsElapsed} "$StartOptionsPhaseTickCount" "$0" $OptionsPhaseSeconds
  ; It is possible for this value to be 0 if the user clicks fast enough so
  ; increment the value by 1 if it is 0.
  ${If} $OptionsPhaseSeconds == 0
    IntOp $OptionsPhaseSeconds $OptionsPhaseSeconds + 1
  ${EndIf}

  ${NSD_GetState} $CheckboxShortcuts $CheckboxShortcuts
  ${NSD_GetText} $DroplistArch $DroplistArch
  ${NSD_GetState} $CheckboxSendPing $CheckboxSendPing
!ifdef MOZ_MAINTENANCE_SERVICE
  ${NSD_GetState} $CheckboxInstallMaintSvc $CheckboxInstallMaintSvc
!endif

FunctionEnd

Function createInstall
  nsDialogs::Create /NOUNLOAD 1018
  Pop $Dialog
  ; Since the text color for controls is set in this Dialog the foreground and
  ; background colors of the Dialog must also be hardcoded.
  SetCtlColors $Dialog ${COMMON_TEXT_COLOR_NORMAL} ${COMMON_BKGRD_COLOR}

  ${NSD_CreateLabel} 0 0 49u 64u ""
  Pop $0
  ${GetDlgItemWidthHeight} $0 $1 $2
  System::Call 'user32::DestroyWindow(i r0)'

  ${NSD_CreateLabel} 0 0 11u 16u ""
  Pop $0
  ${GetDlgItemWidthHeight} $0 $3 $4
  System::Call 'user32::DestroyWindow(i r0)'

  FindWindow $7 "#32770" "" $HWNDPARENT
  ${GetDlgItemWidthHeight} $7 $8 $9

  ; Allow a maximum text width of half of the Dialog's width
  IntOp $R0 $8 / 2

  ${GetTextWidthHeight} "${INSTALL_BLURB1}" $FontBlurb $R0 $5 $6
  IntOp $R1 $1 + $3
  IntOp $R1 $R1 + $5
  IntOp $R1 $8 - $R1
  IntOp $R1 $R1 / 2
  ${NSD_CreateBitmap} $R1 ${INSTALL_BLURB_TOP_DU} 49u 64u ""
  Pop $BitmapBlurb1
  ${SetStretchedTransparentImage} $BitmapBlurb1 $PLUGINSDIR\clock.bmp $HwndBitmapBlurb1
  IntOp $R1 $R1 + $1
  IntOp $R1 $R1 + $3
  ${NSD_CreateLabel} $R1 ${INSTALL_BLURB_TOP_DU} $5 $6 "${INSTALL_BLURB1}"
  Pop $LabelBlurb1
  SendMessage $LabelBlurb1 ${WM_SETFONT} $FontBlurb 0
  SetCtlColors $LabelBlurb1 ${INSTALL_BLURB_TEXT_COLOR} transparent

  ${GetTextWidthHeight} "${INSTALL_BLURB2}" $FontBlurb $R0 $5 $6
  IntOp $R1 $1 + $3
  IntOp $R1 $R1 + $5
  IntOp $R1 $8 - $R1
  IntOp $R1 $R1 / 2
  ${NSD_CreateBitmap} $R1 ${INSTALL_BLURB_TOP_DU} 49u 64u ""
  Pop $BitmapBlurb2
  ${SetStretchedTransparentImage} $BitmapBlurb2 $PLUGINSDIR\particles.bmp $HwndBitmapBlurb2
  IntOp $R1 $R1 + $1
  IntOp $R1 $R1 + $3
  ${NSD_CreateLabel} $R1 ${INSTALL_BLURB_TOP_DU} $5 $6 "${INSTALL_BLURB2}"
  Pop $LabelBlurb2
  SendMessage $LabelBlurb2 ${WM_SETFONT} $FontBlurb 0
  SetCtlColors $LabelBlurb2 ${INSTALL_BLURB_TEXT_COLOR} transparent
  ShowWindow $BitmapBlurb2 ${SW_HIDE}
  ShowWindow $LabelBlurb2 ${SW_HIDE}

  ${GetTextWidthHeight} "${INSTALL_BLURB3}" $FontBlurb $R0 $5 $6
  IntOp $R1 $1 + $3
  IntOp $R1 $R1 + $5
  IntOp $R1 $8 - $R1
  IntOp $R1 $R1 / 2
  ${NSD_CreateBitmap} $R1 ${INSTALL_BLURB_TOP_DU} 49u 64u ""
  Pop $BitmapBlurb3
  ${SetStretchedTransparentImage} $BitmapBlurb3 $PLUGINSDIR\pencil.bmp $HWndBitmapBlurb3
  IntOp $R1 $R1 + $1
  IntOp $R1 $R1 + $3
  ${NSD_CreateLabel} $R1 ${INSTALL_BLURB_TOP_DU} $5 $6 "${INSTALL_BLURB3}"
  Pop $LabelBlurb3
  SendMessage $LabelBlurb3 ${WM_SETFONT} $FontBlurb 0
  SetCtlColors $LabelBlurb3 ${INSTALL_BLURB_TEXT_COLOR} transparent
  ShowWindow $BitmapBlurb3 ${SW_HIDE}
  ShowWindow $LabelBlurb3 ${SW_HIDE}

  ${NSD_CreateProgressBar} 103u 166u 241u 9u ""
  Pop $Progressbar
  ${NSD_AddStyle} $Progressbar ${PBS_MARQUEE}
  SendMessage $Progressbar ${PBM_SETMARQUEE} 1 \
              $ProgressbarMarqueeIntervalMS ; start=1|stop=0 interval(ms)=+N

  ${NSD_CreateLabelCenter} 103u 180u 241u 20u "$(DOWNLOADING_LABEL)"
  Pop $LabelDownloading
  SendMessage $LabelDownloading ${WM_SETFONT} $FontNormal 0
  SetCtlColors $LabelDownloading ${INSTALL_PROGRESS_TEXT_COLOR_NORMAL} transparent

  ${If} ${FileExists} "$INSTDIR\${FileMainEXE}"
    ${NSD_CreateLabelCenter} 103u 180u 241u 20u "$(UPGRADING_LABEL)"
  ${Else}
    ${NSD_CreateLabelCenter} 103u 180u 241u 20u "$(INSTALLING_LABEL)"
  ${EndIf}
  Pop $LabelInstalling
  SendMessage $LabelInstalling ${WM_SETFONT} $FontNormal 0
  SetCtlColors $LabelInstalling ${INSTALL_PROGRESS_TEXT_COLOR_NORMAL} transparent
  ShowWindow $LabelInstalling ${SW_HIDE}

  ${NSD_CreateBitmap} ${APPNAME_BMP_EDGE_DU} ${APPNAME_BMP_TOP_DU} \
                      ${APPNAME_BMP_WIDTH_DU} ${APPNAME_BMP_HEIGHT_DU} ""
  Pop $2
  ${SetStretchedTransparentImage} $2 $PLUGINSDIR\appname.bmp $0

  GetDlgItem $0 $HWNDPARENT 1 ; Install button
  EnableWindow $0 0
  ShowWindow $0 ${SW_HIDE}

  GetDlgItem $0 $HWNDPARENT 3 ; Back button used for Options
  EnableWindow $0 0
  ShowWindow $0 ${SW_HIDE}

  GetDlgItem $0 $HWNDPARENT 2 ; Cancel button
  SendMessage $0 ${WM_SETTEXT} 0 "STR:$(CANCEL_BUTTON)"
  ; Focus the Cancel button otherwise it isn't possible to tab to it since it is
  ; the only control that can be tabbed to.
  ${NSD_SetFocus} $0
  ; Kill the Cancel button's focus so pressing enter won't cancel the install.
  SendMessage $0 ${WM_KILLFOCUS} 0 0

  ${If} "$CanSetAsDefault" == "true"
    GetDlgItem $0 $HWNDPARENT 10 ; Default browser checkbox
    SendMessage $0 ${BM_GETCHECK} 0 0 $CheckboxSetAsDefault
    EnableWindow $0 0
    ShowWindow $0 ${SW_HIDE}
  ${EndIf}

  GetDlgItem $0 $HWNDPARENT 11
  ${If} ${FileExists} "$INSTDIR\${FileMainEXE}"
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(ONE_MOMENT_UPGRADE)"
  ${Else}
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(ONE_MOMENT_INSTALL)"
  ${EndIf}
  SendMessage $0 ${WM_SETFONT} $FontNormal 0
  SetCtlColors $0 ${FOOTER_CONTROL_TEXT_COLOR_FADED} ${FOOTER_BKGRD_COLOR}
  ShowWindow $0 ${SW_SHOW}

  ; Set $DownloadReset to true so the first download tick count is measured.
  StrCpy $DownloadReset "true"
  StrCpy $IsDownloadFinished "false"
  StrCpy $DownloadRetryCount "0"
  StrCpy $DownloadedBytes "0"
  StrCpy $StartLastDownloadTickCount ""
  StrCpy $EndDownloadPhaseTickCount ""
  StrCpy $DownloadFirstTransferSeconds ""
  StrCpy $ExitCode "${ERR_DOWNLOAD_CANCEL}"
  StrCpy $OpenedDownloadPage "0"

  ClearErrors
  ReadINIStr $ExistingVersion "$INSTDIR\application.ini" "App" "Version"
  ${If} ${Errors}
    StrCpy $ExistingVersion "0"
  ${EndIf}

  ClearErrors
  ReadINIStr $ExistingBuildID "$INSTDIR\application.ini" "App" "BuildID"
  ${If} ${Errors}
    StrCpy $ExistingBuildID "0"
  ${EndIf}

  ${If} ${FileExists} "$LOCALAPPDATA\Mozilla\Firefox"
    StrCpy $ExistingProfile "1"
  ${Else}
    StrCpy $ExistingProfile "0"
  ${EndIf}

  StrCpy $DownloadServerIP ""

  System::Call "kernel32::GetTickCount()l .s"
  Pop $StartDownloadPhaseTickCount

  ${If} ${FileExists} "$INSTDIR\uninstall\uninstall.log"
    StrCpy $InstallTotalSteps ${InstallPaveOverTotalSteps}
  ${Else}
    StrCpy $InstallTotalSteps ${InstallCleanTotalSteps}
  ${EndIf}

  ${ITBL3Create}
  ${ITBL3SetProgressState} "${TBPF_INDETERMINATE}"

  ${NSD_CreateTimer} StartDownload ${DownloadIntervalMS}

  LockWindow off
  nsDialogs::Show

  ${NSD_FreeImage} $0
  ${NSD_FreeImage} $HwndBitmapBlurb1
  ${NSD_FreeImage} $HwndBitmapBlurb2
  ${NSD_FreeImage} $HWndBitmapBlurb3
FunctionEnd

Function StartDownload
  ${NSD_KillTimer} StartDownload
  ${If} $DroplistArch == "$(VERSION_64BIT)"
    InetBgDL::Get "${URLStubDownload64}${URLStubDownloadAppend}" \
                  "$PLUGINSDIR\download.exe" \
                  /CONNECTTIMEOUT 120 /RECEIVETIMEOUT 120 /END
  ${Else}
    InetBgDL::Get "${URLStubDownload32}${URLStubDownloadAppend}" \
                  "$PLUGINSDIR\download.exe" \
                  /CONNECTTIMEOUT 120 /RECEIVETIMEOUT 120 /END
  ${EndIf}
  StrCpy $4 ""
  ${NSD_CreateTimer} OnDownload ${DownloadIntervalMS}
  ${If} ${FileExists} "$INSTDIR\${TO_BE_DELETED}"
    RmDir /r "$INSTDIR\${TO_BE_DELETED}"
  ${EndIf}
FunctionEnd

Function SetProgressBars
  SendMessage $Progressbar ${PBM_SETPOS} $ProgressCompleted 0
  ${ITBL3SetProgressValue} "$ProgressCompleted" "$ProgressTotal"
FunctionEnd

Function RemoveFileProgressCallback
  IntOp $InstallCounterStep $InstallCounterStep + 2
  System::Int64Op $ProgressCompleted + $InstallStepSize
  Pop $ProgressCompleted
  Call SetProgressBars
  System::Int64Op $ProgressCompleted + $InstallStepSize
  Pop $ProgressCompleted
  Call SetProgressBars
FunctionEnd

Function OnDownload
  InetBgDL::GetStats
  # $0 = HTTP status code, 0=Completed
  # $1 = Completed files
  # $2 = Remaining files
  # $3 = Number of downloaded bytes for the current file
  # $4 = Size of current file (Empty string if the size is unknown)
  # /RESET must be used if status $0 > 299 (e.g. failure)
  # When status is $0 =< 299 it is handled by InetBgDL
  StrCpy $DownloadServerIP "$5"
  ${If} $0 > 299
    ${NSD_KillTimer} OnDownload
    IntOp $DownloadRetryCount $DownloadRetryCount + 1
    ${If} "$DownloadReset" != "true"
      StrCpy $DownloadedBytes "0"
      ${NSD_AddStyle} $Progressbar ${PBS_MARQUEE}
      SendMessage $Progressbar ${PBM_SETMARQUEE} 1 \
                  $ProgressbarMarqueeIntervalMS ; start=1|stop=0 interval(ms)=+N
      ${ITBL3SetProgressState} "${TBPF_INDETERMINATE}"
    ${EndIf}
    InetBgDL::Get /RESET /END
    StrCpy $DownloadSizeBytes ""
    StrCpy $DownloadReset "true"

    ${If} $DownloadRetryCount >= ${DownloadMaxRetries}
      StrCpy $ExitCode "${ERR_DOWNLOAD_TOO_MANY_RETRIES}"
      ; Use a timer so the UI has a chance to update
      ${NSD_CreateTimer} DisplayDownloadError ${InstallIntervalMS}
    ${Else}
      ${NSD_CreateTimer} StartDownload ${DownloadRetryIntervalMS}
    ${EndIf}
    Return
  ${EndIf}

  ${If} "$DownloadReset" == "true"
    System::Call "kernel32::GetTickCount()l .s"
    Pop $StartLastDownloadTickCount
    StrCpy $DownloadReset "false"
    ; The seconds elapsed from the start of the download phase until the first
    ; bytes are received are only recorded for the first request so it is
    ; possible to determine connection issues for the first request.
    ${If} "$DownloadFirstTransferSeconds" == ""
      ; Get the seconds elapsed from the start of the download phase until the
      ; first bytes are received.
      ${GetSecondsElapsed} "$StartDownloadPhaseTickCount" "$StartLastDownloadTickCount" $DownloadFirstTransferSeconds
    ${EndIf}
  ${EndIf}

  ${If} "$DownloadSizeBytes" == ""
  ${AndIf} "$4" != ""
    ; Handle the case where the size of the file to be downloaded is less than
    ; the minimum expected size or greater than the maximum expected size at the
    ; beginning of the download.
    ${If} $4 < ${DownloadMinSizeBytes}
    ${OrIf} $4 > ${DownloadMaxSizeBytes}
      ${NSD_KillTimer} OnDownload
      InetBgDL::Get /RESET /END
      StrCpy $DownloadReset "true"

      ${If} $DownloadRetryCount >= ${DownloadMaxRetries}
        ; Use a timer so the UI has a chance to update
        ${NSD_CreateTimer} DisplayDownloadError ${InstallIntervalMS}
      ${Else}
        ${NSD_CreateTimer} StartDownload ${DownloadIntervalMS}
      ${EndIf}
      Return
    ${EndIf}

    StrCpy $DownloadSizeBytes "$4"
    System::Int64Op $4 / 2
    Pop $HalfOfDownload
    System::Int64Op $HalfOfDownload / $InstallTotalSteps
    Pop $InstallStepSize
    SendMessage $Progressbar ${PBM_SETMARQUEE} 0 0 ; start=1|stop=0 interval(ms)=+N
    ${RemoveStyle} $Progressbar ${PBS_MARQUEE}
    System::Int64Op $HalfOfDownload + $DownloadSizeBytes
    Pop $ProgressTotal
    StrCpy $ProgressCompleted 0
    SendMessage $Progressbar ${PBM_SETRANGE32} $ProgressCompleted $ProgressTotal
  ${EndIf}

  ; Don't update the status until after the download starts
  ${If} $2 != 0
  ${AndIf} "$4" == ""
    Return
  ${EndIf}

  ; Handle the case where the downloaded size is greater than the maximum
  ; expected size during the download.
  ${If} $DownloadedBytes > ${DownloadMaxSizeBytes}
    InetBgDL::Get /RESET /END
    StrCpy $DownloadReset "true"

    ${If} $DownloadRetryCount >= ${DownloadMaxRetries}
      ; Use a timer so the UI has a chance to update
      ${NSD_CreateTimer} DisplayDownloadError ${InstallIntervalMS}
    ${Else}
      ${NSD_CreateTimer} StartDownload ${DownloadIntervalMS}
    ${EndIf}
    Return
  ${EndIf}

  ${If} $IsDownloadFinished != "true"
    ${If} $2 == 0
      ${NSD_KillTimer} OnDownload
      StrCpy $IsDownloadFinished "true"
      ; The first step of the install progress bar is determined by the
      ; InstallProgressFirstStep define and provides the user with immediate
      ; feedback.
      StrCpy $InstallCounterStep "${InstallProgressFirstStep}"
      System::Call "kernel32::GetTickCount()l .s"
      Pop $EndDownloadPhaseTickCount

      StrCpy $DownloadedBytes "$DownloadSizeBytes"

      ; When a download has finished handle the case where the  downloaded size
      ; is less than the minimum expected size or greater than the maximum
      ; expected size during the download.
      ${If} $DownloadedBytes < ${DownloadMinSizeBytes}
      ${OrIf} $DownloadedBytes > ${DownloadMaxSizeBytes}
        InetBgDL::Get /RESET /END
        StrCpy $DownloadReset "true"

        ${If} $DownloadRetryCount >= ${DownloadMaxRetries}
          ; Use a timer so the UI has a chance to update
          ${NSD_CreateTimer} DisplayDownloadError ${InstallIntervalMS}
        ${Else}
          ${NSD_CreateTimer} StartDownload ${DownloadIntervalMS}
        ${EndIf}
        Return
      ${EndIf}

      LockWindow on
      ; Update the progress bars first in the UI change so they take affect
      ; before other UI changes.
      StrCpy $ProgressCompleted "$DownloadSizeBytes"
      Call SetProgressBars
      System::Int64Op $InstallStepSize * ${InstallProgressFirstStep}
      Pop $R9
      System::Int64Op $ProgressCompleted + $R9
      Pop $ProgressCompleted
      Call SetProgressBars
      ShowWindow $LabelDownloading ${SW_HIDE}
      ShowWindow $LabelInstalling ${SW_SHOW}
      ShowWindow $LabelBlurb2 ${SW_HIDE}
      ShowWindow $BitmapBlurb2 ${SW_HIDE}
      ShowWindow $LabelBlurb3 ${SW_SHOW}
      ShowWindow $BitmapBlurb3 ${SW_SHOW}
      ; Disable the Cancel button during the install
      GetDlgItem $5 $HWNDPARENT 2
      EnableWindow $5 0
      LockWindow off

      ; Open a handle to prevent modification of the full installer
      StrCpy $R9 "${INVALID_HANDLE_VALUE}"
      System::Call 'kernel32::CreateFileW(w "$PLUGINSDIR\download.exe", \
                                          i ${GENERIC_READ}, \
                                          i ${FILE_SHARE_READ}, i 0, \
                                          i ${OPEN_EXISTING}, i 0, i 0) i .R9'
      StrCpy $HandleDownload "$R9"

      ${If} $HandleDownload == ${INVALID_HANDLE_VALUE}
        StrCpy $ExitCode "${ERR_PREINSTALL_INVALID_HANDLE}"
        StrCpy $0 "0"
        StrCpy $1 "0"
      ${Else}
        CertCheck::VerifyCertTrust "$PLUGINSDIR\download.exe"
        Pop $0
        CertCheck::VerifyCertNameIssuer "$PLUGINSDIR\download.exe" \
                                        "${CertNameDownload}" "${CertIssuerDownload}"
        Pop $1
        ${If} $0 == 0
        ${AndIf} $1 == 0
          StrCpy $ExitCode "${ERR_PREINSTALL_CERT_UNTRUSTED_AND_ATTRIBUTES}"
        ${ElseIf} $0 == 0
          StrCpy $ExitCode "${ERR_PREINSTALL_CERT_UNTRUSTED}"
        ${ElseIf}  $1 == 0
          StrCpy $ExitCode "${ERR_PREINSTALL_CERT_ATTRIBUTES}"
        ${EndIf}
      ${EndIf}

      System::Call "kernel32::GetTickCount()l .s"
      Pop $EndPreInstallPhaseTickCount

      ${If} $0 == 0
      ${OrIf} $1 == 0
        ; Use a timer so the UI has a chance to update
        ${NSD_CreateTimer} DisplayDownloadError ${InstallIntervalMS}
        Return
      ${EndIf}

      ; Instead of extracting the files we use the downloaded installer to
      ; install in case it needs to perform operations that the stub doesn't
      ; know about.
      WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "InstallDirectoryPath" "$INSTDIR"
      ; Don't create the QuickLaunch or Taskbar shortcut from the launched installer
      WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "QuickLaunchShortcut" "false"

      ; Always create a start menu shortcut, so the user always has some way
      ; to access the application.
      WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "StartMenuShortcuts" "true"

      ; Either avoid or force adding a taskbar pin and desktop shortcut
      ; based on the checkbox value.
      ${If} $CheckboxShortcuts == 0
        WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "TaskbarShortcut" "false"
        WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "DesktopShortcut" "false"
      ${Else}
        WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "TaskbarShortcut" "true"
        WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "DesktopShortcut" "true"
      ${EndIf}

!ifdef MOZ_MAINTENANCE_SERVICE
      ${If} $CheckboxInstallMaintSvc == 1
        WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "MaintenanceService" "true"
      ${Else}
        WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "MaintenanceService" "false"
      ${EndIf}
!else
      WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "MaintenanceService" "false"
!endif

      ; Delete the taskbar shortcut history to ensure we do the right thing based on
      ; the config file above.
      ${GetShortcutsLogPath} $0
      Delete "$0"

      GetFunctionAddress $0 RemoveFileProgressCallback
      ${RemovePrecompleteEntries} $0

      ; Delete the install.log and let the full installer create it. When the
      ; installer closes it we can detect that it has completed.
      Delete "$INSTDIR\install.log"

      ; Delete firefox.exe.moz-upgrade and firefox.exe.moz-delete if it exists
      ; since it being present will require an OS restart for the full
      ; installer.
      Delete "$INSTDIR\${FileMainEXE}.moz-upgrade"
      Delete "$INSTDIR\${FileMainEXE}.moz-delete"

      System::Call "kernel32::GetTickCount()l .s"
      Pop $EndPreInstallPhaseTickCount

      Exec "$\"$PLUGINSDIR\download.exe$\" /INI=$PLUGINSDIR\${CONFIG_INI}"
      ${NSD_CreateTimer} CheckInstall ${InstallIntervalMS}
    ${Else}
      ${If} $HalfOfDownload != "true"
      ${AndIf} $3 > $HalfOfDownload
        StrCpy $HalfOfDownload "true"
        LockWindow on
        ShowWindow $LabelBlurb1 ${SW_HIDE}
        ShowWindow $BitmapBlurb1 ${SW_HIDE}
        ShowWindow $LabelBlurb2 ${SW_SHOW}
        ShowWindow $BitmapBlurb2 ${SW_SHOW}
        LockWindow off
      ${EndIf}
      StrCpy $DownloadedBytes "$3"
      StrCpy $ProgressCompleted "$DownloadedBytes"
      Call SetProgressBars
    ${EndIf}
  ${EndIf}
FunctionEnd

Function OnPing
  InetBgDL::GetStats
  # $0 = HTTP status code, 0=Completed
  # $1 = Completed files
  # $2 = Remaining files
  # $3 = Number of downloaded bytes for the current file
  # $4 = Size of current file (Empty string if the size is unknown)
  # /RESET must be used if status $0 > 299 (e.g. failure)
  # When status is $0 =< 299 it is handled by InetBgDL
  ${If} $2 == 0
  ${OrIf} $0 > 299
    ${NSD_KillTimer} OnPing
    ${If} $0 > 299
      InetBgDL::Get /RESET /END
    ${EndIf}
    ; The following will exit the installer
    SetAutoClose true
    StrCpy $R9 "2"
    Call RelativeGotoPage
  ${EndIf}
FunctionEnd

Function CheckInstall
  IntOp $InstallCounterStep $InstallCounterStep + 1
  ${If} $InstallCounterStep >= $InstallTotalSteps
    ${NSD_KillTimer} CheckInstall
    ; Close the handle that prevents modification of the full installer
    System::Call 'kernel32::CloseHandle(i $HandleDownload)'
    StrCpy $ExitCode "${ERR_INSTALL_TIMEOUT}"
    ; Use a timer so the UI has a chance to update
    ${NSD_CreateTimer} DisplayDownloadError ${InstallIntervalMS}
    Return
  ${EndIf}

  System::Int64Op $ProgressCompleted + $InstallStepSize
  Pop $ProgressCompleted
  Call SetProgressBars

  ${If} ${FileExists} "$INSTDIR\install.log"
    Delete "$INSTDIR\install.tmp"
    CopyFiles /SILENT "$INSTDIR\install.log" "$INSTDIR\install.tmp"

    ; The unfocus and refocus that happens approximately here is caused by the
    ; installer calling SHChangeNotify to refresh the shortcut icons.

    ; When the full installer completes the installation the install.log will no
    ; longer be in use.
    ClearErrors
    Delete "$INSTDIR\install.log"
    ${Unless} ${Errors}
      ${NSD_KillTimer} CheckInstall
      ; Close the handle that prevents modification of the full installer
      System::Call 'kernel32::CloseHandle(i $HandleDownload)'
      Rename "$INSTDIR\install.tmp" "$INSTDIR\install.log"
      Delete "$PLUGINSDIR\download.exe"
      Delete "$PLUGINSDIR\${CONFIG_INI}"
      System::Call "kernel32::GetTickCount()l .s"
      Pop $EndInstallPhaseTickCount
      System::Int64Op $InstallStepSize * ${InstallProgressFinishStep}
      Pop $InstallStepSize
      ${NSD_CreateTimer} FinishInstall ${InstallIntervalMS}
    ${EndUnless}
  ${EndIf}
FunctionEnd

Function FinishInstall
  ; The full installer has completed but the progress bar still needs to finish
  ; so increase the size of the step.
  IntOp $InstallCounterStep $InstallCounterStep + ${InstallProgressFinishStep}
  ${If} $InstallTotalSteps < $InstallCounterStep
    StrCpy $InstallCounterStep "$InstallTotalSteps"
  ${EndIf}

  ${If} $InstallTotalSteps != $InstallCounterStep
    System::Int64Op $ProgressCompleted + $InstallStepSize
    Pop $ProgressCompleted
    Call SetProgressBars
    Return
  ${EndIf}

  ${NSD_KillTimer} FinishInstall

  StrCpy $ProgressCompleted "$ProgressTotal"
  Call SetProgressBars

  ${If} "$CheckboxSetAsDefault" == "1"
    ; NB: this code is duplicated in installer.nsi. Please keep in sync.
    ; For data migration in the app, we want to know what the default browser
    ; value was before we changed it. To do so, we read it here and store it
    ; in our own registry key.
    StrCpy $0 ""
    AppAssocReg::QueryCurrentDefault "http" "protocol" "effective"
    Pop $1
    ; If the method hasn't failed, $1 will contain the progid. Check:
    ${If} "$1" != "method failed"
    ${AndIf} "$1" != "method not available"
      ; Read the actual command from the progid
      ReadRegStr $0 HKCR "$1\shell\open\command" ""
    ${EndIf}
    ; If using the App Association Registry didn't happen or failed, fall back
    ; to the effective http default:
    ${If} "$0" == ""
      ReadRegStr $0 HKCR "http\shell\open\command" ""
    ${EndIf}
    ; If we have something other than empty string now, write the value.
    ${If} "$0" != ""
      ClearErrors
      WriteRegStr HKCU "Software\Mozilla\Firefox" "OldDefaultBrowserCommand" "$0"
    ${EndIf}

    ${GetParameters} $0
    ClearErrors
    ${GetOptions} "$0" "/UAC:" $0
    ${If} ${Errors} ; Not elevated
      Call ExecSetAsDefaultAppUser
    ${Else} ; Elevated - execute the function in the unelevated process
      GetFunctionAddress $0 ExecSetAsDefaultAppUser
      UAC::ExecCodeSegment $0
    ${EndIf}
  ${EndIf}

  ${If} ${FileExists} "$INSTDIR\${FileMainEXE}.moz-upgrade"
    Delete "$INSTDIR\${FileMainEXE}"
    Rename "$INSTDIR\${FileMainEXE}.moz-upgrade" "$INSTDIR\${FileMainEXE}"
  ${EndIf}

  StrCpy $ExitCode "${ERR_SUCCESS}"

  StrCpy $InstallCounterStep 0
  ${NSD_CreateTimer} FinishProgressBar ${InstallIntervalMS}
FunctionEnd

Function FinishProgressBar
  IntOp $InstallCounterStep $InstallCounterStep + 1

  ${If} $InstallCounterStep < 10
    Return
  ${EndIf}

  ${NSD_KillTimer} FinishProgressBar

  Call CopyPostSigningData
  Call LaunchApp
  Call SendPing
FunctionEnd

Function OnBack
  StrCpy $WasOptionsButtonClicked "1"
  StrCpy $R9 "1" ; Goto the next page
  Call RelativeGotoPage
  ; The call to Abort prevents NSIS from trying to move to the previous or the
  ; next page.
  Abort
FunctionEnd

Function RelativeGotoPage
  IntCmp $R9 0 0 Move Move
  StrCmp $R9 "X" 0 Move
  StrCpy $R9 "120"

  Move:
  SendMessage $HWNDPARENT "0x408" "$R9" ""
FunctionEnd

Function UpdateFreeSpaceLabel
  ; Only update when $ExistingTopDir isn't set
  ${If} "$ExistingTopDir" != ""
    StrLen $5 "$ExistingTopDir"
    StrLen $6 "$INSTDIR"
    ${If} $5 <= $6
      StrCpy $7 "$INSTDIR" $5
      ${If} "$7" == "$ExistingTopDir"
        Return
      ${EndIf}
    ${EndIf}
  ${EndIf}

  Call CheckSpace

  StrCpy $0 "$SpaceAvailableBytes"

  StrCpy $1 "$(BYTE)"

  ${If} $0 > 1024
  ${OrIf} $0 < 0
    System::Int64Op $0 / 1024
    Pop $0
    StrCpy $1 "$(KILO)$(BYTE)"
    ${If} $0 > 1024
    ${OrIf} $0 < 0
      System::Int64Op $0 / 1024
      Pop $0
      StrCpy $1 "$(MEGA)$(BYTE)"
      ${If} $0 > 1024
      ${OrIf} $0 < 0
        System::Int64Op $0 / 1024
        Pop $0
        StrCpy $1 "$(GIGA)$(BYTE)"
      ${EndIf}
    ${EndIf}
  ${EndIf}

  SendMessage $LabelFreeSpace ${WM_SETTEXT} 0 "STR:$0 $1"
FunctionEnd

Function OnChange_DirRequest
  Pop $0
  System::Call 'user32::GetWindowTextW(i $DirRequest, w .r0, i ${NSIS_MAX_STRLEN})'
  StrCpy $1 "$0" 1 ; the first character
  ${If} "$1" == "$\""
    StrCpy $1 "$0" "" -1 ; the last character
    ${If} "$1" == "$\""
      StrCpy $0 "$0" "" 1 ; all but the first character
      StrCpy $0 "$0" -1 ; all but the last character
    ${EndIf}
  ${EndIf}

  StrCpy $INSTDIR "$0"
  Call UpdateFreeSpaceLabel

  GetDlgItem $0 $HWNDPARENT 1 ; Install button
  ${If} ${FileExists} "$INSTDIR\${FileMainEXE}"
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(UPGRADE_BUTTON)"
  ${Else}
    SendMessage $0 ${WM_SETTEXT} 0 "STR:$(INSTALL_BUTTON)"
  ${EndIf}
FunctionEnd

Function OnClick_ButtonBrowse
  StrCpy $0 "$INSTDIR"
  nsDialogs::SelectFolderDialog /NOUNLOAD "$(SELECT_FOLDER_TEXT)" $0
  Pop $0
  ${If} $0 == "error" ; returns 'error' if 'cancel' was pressed?
    Return
  ${EndIf}

  ${If} $0 != ""
    StrCpy $INSTDIR "$0"
    System::Call 'user32::SetWindowTextW(i $DirRequest, w "$INSTDIR")'
  ${EndIf}
FunctionEnd

Function OnChange_DroplistArch
  ; When the user changes the 32/64-bit setting, change the default install path
  ; to use the correct version of Program Files. But only do that if the user
  ; hasn't selected their own install path yet, and if we didn't select our
  ; default as the location of an existing install.
  ${If} $INSTDIR == $InitialInstallDir
    ${NSD_GetText} $DroplistArch $0
    ${If} $0 == "$(VERSION_32BIT)"
      ${If} $PreviousInstallArch == 32
        StrCpy $InitialInstallDir $PreviousInstallDir
      ${Else}
        StrCpy $InitialInstallDir "${DefaultInstDir32bit}"
      ${EndIf}
    ${Else}
      ${If} $PreviousInstallArch == 64
        StrCpy $InitialInstallDir $PreviousInstallDir
      ${Else}
        StrCpy $InitialInstallDir "${DefaultInstDir64bit}"
      ${EndIf}
    ${EndIf}
    ${NSD_SetText} $DirRequest $InitialInstallDir
  ${EndIf}
FunctionEnd

Function CheckSpace
  ${If} "$ExistingTopDir" != ""
    StrLen $0 "$ExistingTopDir"
    StrLen $1 "$INSTDIR"
    ${If} $0 <= $1
      StrCpy $2 "$INSTDIR" $3
      ${If} "$2" == "$ExistingTopDir"
        Return
      ${EndIf}
    ${EndIf}
  ${EndIf}

  StrCpy $ExistingTopDir "$INSTDIR"
  ${DoUntil} ${FileExists} "$ExistingTopDir"
    ${GetParent} "$ExistingTopDir" $ExistingTopDir
    ${If} "$ExistingTopDir" == ""
      StrCpy $SpaceAvailableBytes "0"
      StrCpy $HasRequiredSpaceAvailable "false"
      Return
    ${EndIf}
  ${Loop}

  ${GetLongPath} "$ExistingTopDir" $ExistingTopDir

  ; GetDiskFreeSpaceExW requires a backslash.
  StrCpy $0 "$ExistingTopDir" "" -1 ; the last character
  ${If} "$0" != "\"
    StrCpy $0 "\"
  ${Else}
    StrCpy $0 ""
  ${EndIf}

  System::Call 'kernel32::GetDiskFreeSpaceExW(w, *l, *l, *l) i("$ExistingTopDir$0", .r1, .r2, .r3) .'
  StrCpy $SpaceAvailableBytes "$1"

  System::Int64Op $SpaceAvailableBytes / 1048576
  Pop $1
  System::Int64Op $1 > ${APPROXIMATE_REQUIRED_SPACE_MB}
  Pop $1
  ${If} $1 == 1
    StrCpy $HasRequiredSpaceAvailable "true"
  ${Else}
    StrCpy $HasRequiredSpaceAvailable "false"
  ${EndIf}
FunctionEnd

Function CanWrite
  StrCpy $CanWriteToInstallDir "false"

  StrCpy $0 "$INSTDIR"
  ; Use the existing directory when it exists
  ${Unless} ${FileExists} "$INSTDIR"
    ; Get the topmost directory that exists for new installs
    ${DoUntil} ${FileExists} "$0"
      ${GetParent} "$0" $0
      ${If} "$0" == ""
        Return
      ${EndIf}
    ${Loop}
  ${EndUnless}

  GetTempFileName $2 "$0"
  Delete $2
  CreateDirectory "$2"

  ${If} ${FileExists} "$2"
    ${If} ${FileExists} "$INSTDIR"
      GetTempFileName $3 "$INSTDIR"
    ${Else}
      GetTempFileName $3 "$2"
    ${EndIf}
    ${If} ${FileExists} "$3"
      Delete "$3"
      StrCpy $CanWriteToInstallDir "true"
    ${EndIf}
    RmDir "$2"
  ${EndIf}
FunctionEnd

Function ExecSetAsDefaultAppUser
  ; Using the helper.exe lessens the stub installer size.
  ; This could ask for elevatation when the user doesn't install as admin.
  Exec "$\"$INSTDIR\uninstall\helper.exe$\" /SetAsDefaultAppUser"
FunctionEnd

Function LaunchApp
!ifndef DEV_EDITION
  FindWindow $0 "${WindowClass}"
  ${If} $0 <> 0 ; integer comparison
    StrCpy $FirefoxLaunchCode "1"
    MessageBox MB_OK|MB_ICONQUESTION "$(WARN_MANUALLY_CLOSE_APP_LAUNCH)"
    Return
  ${EndIf}
!endif

  StrCpy $FirefoxLaunchCode "2"

  ; Set the current working directory to the installation directory
  SetOutPath "$INSTDIR"
  ClearErrors
  ${GetParameters} $0
  ${GetOptions} "$0" "/UAC:" $1
  ${If} ${Errors}
    Exec "$\"$INSTDIR\${FileMainEXE}$\""
  ${Else}
    GetFunctionAddress $0 LaunchAppFromElevatedProcess
    UAC::ExecCodeSegment $0
  ${EndIf}
FunctionEnd

Function LaunchAppFromElevatedProcess
  ; Set the current working directory to the installation directory
  SetOutPath "$INSTDIR"
  Exec "$\"$INSTDIR\${FileMainEXE}$\""
FunctionEnd

Function CopyPostSigningData
  ${LineRead} "$EXEDIR\postSigningData" "1" $PostSigningData
  ${If} ${Errors}
    ClearErrors
    StrCpy $PostSigningData "0"
  ${Else}
    CreateDirectory "$LOCALAPPDATA\Mozilla\Firefox"
    CopyFiles /SILENT "$EXEDIR\postSigningData" "$LOCALAPPDATA\Mozilla\Firefox"
  ${Endif}
FunctionEnd

Function DisplayDownloadError
  ${NSD_KillTimer} DisplayDownloadError
  ; To better display the error state on the taskbar set the progress completed
  ; value to the total value.
  ${ITBL3SetProgressValue} "100" "100"
  ${ITBL3SetProgressState} "${TBPF_ERROR}"
  MessageBox MB_OKCANCEL|MB_ICONSTOP "$(ERROR_DOWNLOAD)" IDCANCEL +2 IDOK +1
  StrCpy $OpenedDownloadPage "1" ; Already initialized to 0

  ${If} "$OpenedDownloadPage" == "1"
    ClearErrors
    ${GetParameters} $0
    ${GetOptions} "$0" "/UAC:" $1
    ${If} ${Errors}
      Call OpenManualDownloadURL
    ${Else}
      GetFunctionAddress $0 OpenManualDownloadURL
      UAC::ExecCodeSegment $0
    ${EndIf}
  ${EndIf}

  Call SendPing
FunctionEnd

Function OpenManualDownloadURL
  ExecShell "open" "${URLManualDownload}${URLManualDownloadAppend}"
FunctionEnd

Section
SectionEnd
