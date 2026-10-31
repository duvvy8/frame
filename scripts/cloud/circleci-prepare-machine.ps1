$ErrorActionPreference = 'Stop'
if ($env:CIRCLECI -ne 'true') { throw 'CircleCI cloud runner required. Local execution is forbidden.' }
New-Item -ItemType Directory -Force -Path 'C:\frame-diagnostics' | Out-Null
# This is a fresh CircleCI VM. Reclaim only allowlisted preinstalled caches,
# unused .NET SDKs and obsolete Windows SDK versions; never user data or OS files.
$allowlisted = @('C:\ProgramData\Package Cache',
    'C:\ProgramData\Microsoft\VisualStudio\Packages','C:\Program Files\dotnet',
    'C:\Program Files\Microsoft Visual Studio\2022',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022')
foreach ($sdkPart in @('Include','Lib','bin','UnionMetadata','References')) {
    $sdkParent = 'C:\Program Files (x86)\Windows Kits\10\' + $sdkPart
    foreach ($sdkVersion in @('10.0.10240.0','10.0.19041.0','10.0.22621.0','10.0.26100.0')) {
        $allowlisted += Join-Path $sdkParent $sdkVersion
    }
}
foreach ($cleanupPath in $allowlisted) {
    if (Test-Path -LiteralPath $cleanupPath) {
        $resolved = (Resolve-Path -LiteralPath $cleanupPath).Path
        if ($resolved -ne $cleanupPath -or $resolved -match '^C:\\frame-') { throw 'Unexpected cleanup path' }
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction Continue
    }
}
function Install-SignedMicrosoftTool([string]$Url,[string]$Name,[string[]]$Arguments) {
    $installerPath = Join-Path 'C:\frame-diagnostics' $Name
    Invoke-WebRequest -Uri $Url -OutFile $installerPath -UseBasicParsing
    $signature = Get-AuthenticodeSignature -LiteralPath $installerPath
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') {
        throw 'Microsoft installer signature verification failed'
    }
    $installed = Start-Process -FilePath $installerPath -ArgumentList $Arguments -WindowStyle Hidden -Wait -PassThru
    if ($installed.ExitCode -notin @(0,3010)) { throw ('Cloud toolchain installer failed: ' + $installed.ExitCode) }
    Remove-Item -LiteralPath $installerPath
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs2026 = ''
if (Test-Path -LiteralPath $vswhere) {
    $vs2026 = (& $vswhere -latest -products '*' -version '[18.0,19.0)' -property installationPath | Out-String).Trim()
}
if (-not $vs2026) {
    Install-SignedMicrosoftTool 'https://aka.ms/vs/stable/vs_buildtools.exe' 'vs_buildtools.exe' @(
        '--quiet','--wait','--norestart','--nocache','--installPath','C:\FrameCloudBuildTools',
        '--add','Microsoft.VisualStudio.Workload.VCTools','--add','Microsoft.VisualStudio.Component.VC.ATLMFC','--includeRecommended')
}
if (-not (Test-Path -LiteralPath 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.28000.0\um\Windows.h')) {
    Install-SignedMicrosoftTool 'https://go.microsoft.com/fwlink/?linkid=2382519' 'winsdksetup.exe' @(
        '/quiet','/norestart','/features','OptionId.DesktopCPPx64','OptionId.DesktopCPPx86')
}
$freeGiB = [math]::Round((Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='C:'").FreeSpace/1GB,2)
[ordered]@{freeGiB=$freeGiB;cloudOnly=$true;paidFallback=$false} | ConvertTo-Json |
    Tee-Object -FilePath 'C:\frame-diagnostics\machine-prepared.json'
if ($freeGiB -lt 100) { throw 'Cloud image still has less than Chromium minimum free disk space. Stopping without a paid fallback.' }
