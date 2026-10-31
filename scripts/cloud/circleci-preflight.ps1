$ErrorActionPreference = 'Stop'
if ($env:CIRCLECI -ne 'true') { throw 'CircleCI cloud runner required. Local execution is forbidden.' }
New-Item -ItemType Directory -Force -Path 'C:\frame-diagnostics' | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$studio = @()
if (Test-Path -LiteralPath $vswhere) {
    $installed = & $vswhere -all -products '*' -format json | ConvertFrom-Json
    $studio = @($installed | ForEach-Object {
        [pscustomobject]@{installationVersion=$_.installationVersion;installationPath=$_.installationPath}
    })
}
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
$sdks = @()
if (Test-Path -LiteralPath $sdkRoot) {
    $sdks = @(Get-ChildItem -LiteralPath $sdkRoot -Directory | Select-Object -ExpandProperty Name)
}
$drives = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' |
    Select-Object DeviceID,@{n='freeGiB';e={[math]::Round($_.FreeSpace/1GB,2)}},@{n='sizeGiB';e={[math]::Round($_.Size/1GB,2)}})
$memory = Get-CimInstance Win32_ComputerSystem
$spaceCandidates = @()
foreach ($candidatePath in @('C:\ProgramData\Package Cache','C:\ProgramData\Microsoft\VisualStudio\Packages',
    'C:\Program Files\Microsoft Visual Studio','C:\Program Files (x86)\Microsoft Visual Studio',
    'C:\Program Files (x86)\Windows Kits','C:\Program Files\dotnet','C:\Android','C:\tools','C:\ProgramData\chocolatey')) {
    if (Test-Path -LiteralPath $candidatePath) {
        $bytes = (Get-ChildItem -LiteralPath $candidatePath -File -Recurse -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
        $spaceCandidates += [pscustomobject]@{path=$candidatePath;GiB=[math]::Round($bytes/1GB,2)}
    }
}
$record = [ordered]@{
    cloudOnly = $true
    computer = $env:COMPUTERNAME
    cpuThreads = $env:NUMBER_OF_PROCESSORS
    memoryGiB = [math]::Round($memory.TotalPhysicalMemory/1GB,2)
    drives = $drives
    visualStudio = $studio
    sdkVersions = $sdks
    spaceCandidates = $spaceCandidates
    gitVersion = (& git --version)
    pythonVersion = (& python --version 2>&1 | Out-String).Trim()
    nativeCompilerInvoked = $false
    paidFallback = $false
}
$record | ConvertTo-Json -Depth 8 | Tee-Object -FilePath 'C:\frame-diagnostics\preflight.json'
