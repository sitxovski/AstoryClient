# Copyright (c) 2026 @sitxovski (Discord)
# Astory Client - install what is needed to build the game, the launcher and the installer, and to run the tools.
#   install_prerequisites.bat         asks before each package
#   install_prerequisites.bat -Yes    installs everything without questions
param([switch]$Yes)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\_setup\install_common.ps1')
if (-not (Test-Winget)) { exit 1 }

# Visual Studio 2022 with the workloads Unreal Engine and the launcher need
$vs = '--add Microsoft.VisualStudio.Workload.NativeGame --add Microsoft.VisualStudio.Workload.NativeDesktop ' +
      '--add Microsoft.VisualStudio.Workload.ManagedDesktop --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ' +
      '--add Microsoft.VisualStudio.Component.Windows11SDK.22621 --add Microsoft.Net.Component.4.8.SDK ' +
      '--add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended --passive --wait'
$pkgs = @(
    @{ Name = 'Visual Studio 2022 Community (C++ game + desktop workloads)'; Id = 'Microsoft.VisualStudio.2022.Community'; Override = $vs;
       Why = 'compiles the game modules (Unreal Engine 5.8), the launcher and the editor plugins';
       Note = 'already installed: open Visual Studio Installer -> Modify and tick Game development with C++, Desktop development with C++, .NET desktop development, Windows 11 SDK 22621, C++ CMake tools' },
    @{ Name = 'Epic Games Launcher'; Id = 'EpicGames.EpicGamesLauncher'; Why = 'install Unreal Engine 5.8 from it (Unreal Engine tab -> Library -> +)' },
    @{ Name = 'Python 3.13'; Id = 'Python.Python.3.13'; Why = 'build / release / data tools (AstoryClient\Tools, DEPLOY)' },
    @{ Name = 'Git'; Id = 'Git.Git'; Why = 'getting the sources' },
    @{ Name = 'CMake'; Id = 'Kitware.CMake'; Why = 'builds the launcher (AstoryClient\Launcher)' },
    @{ Name = 'Ninja'; Id = 'Ninja-build.Ninja'; Why = 'launcher build generator' },
    @{ Name = 'Inno Setup 6'; Id = 'JRSoftware.InnoSetup'; Why = 'builds the player installer (DEPLOY\Installer)' }
)
$failed = Install-Packages -Packages $pkgs -Yes:$Yes
$pyok = Install-PythonRequirements (Join-Path $PSScriptRoot 'requirements.txt')

Show-Manual @(
    'Unreal Engine 5.8: Epic Games Launcher -> Unreal Engine -> Library -> Engine Versions -> + -> 5.8 -> Install.',
    'Game resources: download "AstoryClient Resources" (https://drive.google.com/drive/folders/1p9x3ekIwU1PdqFaRMMeuR9ugiUC9DaT9?usp=sharing) and copy its Content folder into AstoryClient\ (result: AstoryClient\Content\AsdaAssets\...).',
    'Run _setup\configure.bat in the repository root (Epic IDs, hosts, keys) and say "y" to generate the pak keys.',
    'Right-click AstoryClient\AsdaReborn.uproject -> "Switch Unreal Engine version..." -> 5.8, then "Generate Visual Studio project files".',
    'EOS SDK 1.19.2.1 (Epic Developer Portal -> SDK Download): unpack into <ASTORY_DEV_ROOT>\ThirdParty\EOS-SDK-58105819-Release-v1.19.2.1 and copy its SDK\Lib and SDK\Bin into AstoryClient\Source\ThirdParty\AsdaEOSSDK\SDK\. Enable Easy Anti-Cheat for your product in the portal.'
)
if ($failed) { Write-Host ('Failed: ' + ($failed -join ', ')) -ForegroundColor Red }
if (-not $pyok) { Write-Host 'Python packages were not installed - see the message above.' -ForegroundColor Yellow }
