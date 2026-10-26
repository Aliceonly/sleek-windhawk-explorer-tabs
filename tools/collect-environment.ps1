#requires -Version 5.1
<#
.SYNOPSIS
Collect read-only Windows/Explorer evidence for Sleek WinTabs acceptance.
.DESCRIPTION
Writes JSON to the success stream. -OutFile additionally creates a NEW UTF-8
file in an existing directory; existing files are never overwritten. No other
files, registry values, services, processes, or mod settings are changed.

Only allowlisted OS/installation registry values are read. Process command
lines, owners, credentials, open folders, and mod configuration are not read.
Paths and PID/session IDs in the report may still identify a local account.

This is a non-atomic observation, not a compatibility verdict. Disk candidates
are not proof of which DLL is loaded. Module enumeration is best effort and
can fail across bitness, security, or process-exit boundaries.
.PARAMETER ProbeWindhawk
Opt in to bounded installation discovery: common uninstall registry keys in
HKLM/HKCU, both applicable registry views, and conventional install paths.
An installation directory is confirmed only when windhawk.exe is found there.
Arbitrary portable locations are not searched; no executable is launched.
.PARAMETER OutFile
Optional NEW JSON file. The parent directory must already exist.
.EXAMPLE
.\tools\collect-environment.ps1
.EXAMPLE
.\tools\collect-environment.ps1 -ProbeWindhawk -OutFile "$env:TEMP\sleek-environment.json"
#>
[CmdletBinding()]
param(
    [string]$OutFile,
    [switch]$ProbeWindhawk
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Get-ProbeFailure {
    param([System.Management.Automation.ErrorRecord]$Record)
    $exception = $Record.Exception
    while ($null -ne $exception.InnerException) {
        $exception = $exception.InnerException
    }
    $status = 'error'
    if ($exception -is [System.UnauthorizedAccessException] -or
        $exception -is [System.Security.SecurityException] -or
        $Record.CategoryInfo.Category -eq 'PermissionDenied') {
        $status = 'access_denied'
    } elseif ($exception -is [System.IO.FileNotFoundException] -or
              $exception -is [System.IO.DirectoryNotFoundException] -or
              $Record.CategoryInfo.Category -eq 'ObjectNotFound') {
        $status = 'not_found'
    }
    return [pscustomobject]@{
        status = $status
        errorType = $exception.GetType().FullName
        message = $exception.Message
    }
}

function Read-RegistrySnapshot {
    param(
        [Microsoft.Win32.RegistryHive]$Hive,
        [Microsoft.Win32.RegistryView]$View,
        [string]$SubKey,
        [string[]]$Names
    )
    $result = [ordered]@{
        hive = $Hive.ToString(); view = $View.ToString(); key = $SubKey
        status = 'not_found'; values = $null; error = $null
    }
    $baseKey = $null
    $key = $null
    try {
        $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive, $View)
        $key = $baseKey.OpenSubKey($SubKey, $false)
        if ($null -ne $key) {
            $values = [ordered]@{}
            foreach ($name in $Names) {
                $value = $key.GetValue($name, $null,
                    [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                $valueStatus = 'found'
                if ($null -eq $value) { $valueStatus = 'not_found' }
                $values[$name] = [pscustomobject]@{ status = $valueStatus; value = $value }
            }
            $result.status = 'found'
            $result.values = [pscustomobject]$values
        }
    } catch {
        $result.error = Get-ProbeFailure $_
        $result.status = $result.error.status
    } finally {
        if ($null -ne $key) { $key.Dispose() }
        if ($null -ne $baseKey) { $baseKey.Dispose() }
    }
    return [pscustomobject]$result
}

function Read-PeMachine {
    param([string]$Path)
    $stream = $null
    $reader = $null
    try {
        $share = [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read, $share)
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) {
            throw [System.IO.InvalidDataException]::new('Missing DOS header.')
        }
        $stream.Position = 0x3C
        $offset = $reader.ReadUInt32()
        if ($offset -lt 64 -or ([long]$offset + 6) -gt $stream.Length) {
            throw [System.IO.InvalidDataException]::new('Invalid PE header offset.')
        }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw [System.IO.InvalidDataException]::new('Missing PE signature.')
        }
        $machine = $reader.ReadUInt16()
        $architecture = switch ($machine) {
            0x014C { 'x86' }
            0x8664 { 'x64' }
            0xAA64 { 'ARM64' }
            0xA641 { 'ARM64EC' }
            default { 'unknown' }
        }
        return [pscustomobject]@{
            status = 'found'; machine = ('0x{0:X4}' -f $machine)
            architecture = $architecture
        }
    } catch {
        return Get-ProbeFailure $_
    } finally {
        if ($null -ne $reader) { $reader.Dispose() }
        elseif ($null -ne $stream) { $stream.Dispose() }
    }
}

function Read-FileEvidence {
    param([string]$Path)
    $readPath = $Path
    if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT -and
        [Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
        $windowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
        $nativePrefix = [System.IO.Path]::Combine($windowsRoot, 'System32') + '\'
        if ($Path.StartsWith($nativePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            # Module paths also need this translation, not just known candidates.
            $readPath = [System.IO.Path]::Combine($windowsRoot, 'Sysnative\' + $Path.Substring($nativePrefix.Length))
        }
    }
    $result = [ordered]@{
        path = $Path; readPath = $readPath; status = 'not_found'; length = $null
        lastWriteTimeUtc = $null; version = $null; sha256 = $null
        peMachine = $null; error = $null
    }
    try {
        $item = Get-Item -LiteralPath $readPath -Force -ErrorAction Stop
        if ($item.PSIsContainer) {
            $result.error = 'Expected a file, found a directory.'
            return [pscustomobject]$result
        }
        $result.status = 'found'
        $result.length = $item.Length
        $result.lastWriteTimeUtc = $item.LastWriteTimeUtc.ToString('o')
        try {
            $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($readPath)
            $versionStatus = 'found'
            if ([string]::IsNullOrEmpty($version.FileVersion)) { $versionStatus = 'not_found' }
            $result.version = [pscustomobject]@{
                status = $versionStatus; fileVersion = $version.FileVersion
                productVersion = $version.ProductVersion
            }
        } catch { $result.version = Get-ProbeFailure $_ }
        try {
            $hash = Get-FileHash -LiteralPath $readPath -Algorithm SHA256 -ErrorAction Stop
            $result.sha256 = [pscustomobject]@{ status = 'found'; value = $hash.Hash }
        } catch { $result.sha256 = Get-ProbeFailure $_ }
        $result.peMachine = Read-PeMachine $readPath
    } catch {
        $result.error = Get-ProbeFailure $_
        $result.status = $result.error.status
    }
    return [pscustomobject]$result
}

function Read-ArtifactCandidates {
    param([string[]]$Paths, [bool]$DiscoveryIncomplete = $false)
    $items = @()
    foreach ($path in @($Paths | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Sort-Object -Unique)) {
        $items += Read-FileEvidence $path
    }
    $status = 'not_found'
    if (@($items | Where-Object { $_.status -eq 'found' }).Count -gt 0) {
        $status = 'found'
    } elseif ($DiscoveryIncomplete -or
              @($items | Where-Object { $_.status -ne 'not_found' }).Count -gt 0) {
        $status = 'incomplete'
    }
    return [pscustomobject]@{ status = $status; candidates = @($items) }
}

function Find-Windhawk {
    $registry = @()
    $directories = @()
    $views = @([Microsoft.Win32.RegistryView]::Registry32)
    if ([Environment]::Is64BitOperatingSystem) {
        $views += [Microsoft.Win32.RegistryView]::Registry64
    }
    foreach ($hive in @('LocalMachine', 'CurrentUser')) {
        foreach ($view in $views) {
            $snapshot = Read-RegistrySnapshot -Hive $hive -View $view -SubKey (
                'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Windhawk'
            ) -Names @('DisplayName', 'DisplayVersion', 'InstallLocation')
            $registry += $snapshot
            if ($snapshot.status -eq 'found') {
                $location = $snapshot.values.InstallLocation.value
                if ($location -is [string] -and -not [string]::IsNullOrWhiteSpace($location)) {
                    $location = [Environment]::ExpandEnvironmentVariables($location.Trim().Trim('"'))
                    if ([System.IO.Path]::IsPathRooted($location)) { $directories += $location }
                }
            }
        }
    }
    foreach ($variable in @('ProgramW6432', 'ProgramFiles', 'ProgramFiles(x86)')) {
        $root = [Environment]::GetEnvironmentVariable($variable)
        if (-not [string]::IsNullOrEmpty($root)) {
            $directories += [System.IO.Path]::Combine($root, 'Windhawk')
        }
    }
    $localAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
    if (-not [string]::IsNullOrEmpty($localAppData)) {
        $directories += [System.IO.Path]::Combine($localAppData, 'Programs\Windhawk')
        $directories += [System.IO.Path]::Combine($localAppData, 'Windhawk')
    }
    $candidates = @()
    foreach ($directory in @($directories | Sort-Object -Unique)) {
        $executable = Read-FileEvidence ([System.IO.Path]::Combine($directory, 'windhawk.exe'))
        $candidates += [pscustomobject]@{
            directory = $directory; status = $executable.status; executable = $executable
        }
    }
    $status = 'not_found'
    if (@($candidates | Where-Object { $_.status -eq 'found' }).Count -gt 0) {
        $status = 'found'
    } elseif (@($registry + $candidates | Where-Object {
            $_.status -notin @('found', 'not_found') }).Count -gt 0) {
        $status = 'incomplete'
    }
    return [pscustomobject]@{
        status = $status
        scope = 'Exact Uninstall\Windhawk keys and conventional paths only; portable locations may be missed.'
        registry = @($registry); candidates = @($candidates)
    }
}

$isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
$report = [ordered]@{
    schemaVersion = 1
    collectedAtUtc = [DateTime]::UtcNow.ToString('o')
    collector = [pscustomobject]@{
        powershellVersion = $PSVersionTable.PSVersion.ToString()
        is64BitProcess = [Environment]::Is64BitProcess
        is64BitOperatingSystem = [Environment]::Is64BitOperatingSystem
        processArchitectureEnvironment = [Environment]::GetEnvironmentVariable('PROCESSOR_ARCHITECTURE')
        isWindows = $isWindowsHost
        mode = 'read_only_except_explicit_OutFile'
    }
    os = [pscustomobject]@{ status = 'unsupported_platform' }
    files = [pscustomobject]@{ status = 'unsupported_platform' }
    explorerProcesses = [pscustomobject]@{ status = 'unsupported_platform'; items = @() }
    windhawk = [pscustomobject]@{ status = 'not_requested' }
    notes = @(
        'No credentials, command lines, process owners, open folders, or mod settings are collected.',
        'This is not an atomic snapshot or a mod compatibility verdict.',
        'not_found is limited to the reported search scope, not proof of absence everywhere.',
        'File versions and SHA256 describe disk files; loaded-module paths are separate evidence.',
        'Installation paths and error messages may contain local account names.'
    )
}

if ($isWindowsHost) {
    $nativeView = [Microsoft.Win32.RegistryView]::Registry32
    if ([Environment]::Is64BitOperatingSystem) { $nativeView = [Microsoft.Win32.RegistryView]::Registry64 }
    $osRegistry = Read-RegistrySnapshot -Hive LocalMachine -View $nativeView -SubKey (
        'SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    ) -Names @('ProductName', 'DisplayVersion', 'CurrentBuildNumber', 'UBR', 'BuildLabEx', 'EditionID')
    $architectureRegistry = Read-RegistrySnapshot -Hive LocalMachine -View $nativeView -SubKey (
        'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
    ) -Names @('PROCESSOR_ARCHITECTURE')
    try {
        $os = Get-CimInstance -ClassName Win32_OperatingSystem -Property Caption, Version, BuildNumber, OSArchitecture
        $osCim = [pscustomobject]@{
            status = 'found'; caption = $os.Caption; version = $os.Version
            buildNumber = $os.BuildNumber; architecture = $os.OSArchitecture
        }
    } catch { $osCim = Get-ProbeFailure $_ }
    $fullBuild = $null
    if ($osRegistry.status -eq 'found' -and
        $osRegistry.values.CurrentBuildNumber.status -eq 'found' -and
        $osRegistry.values.UBR.status -eq 'found') {
        $fullBuild = '{0}.{1}' -f $osRegistry.values.CurrentBuildNumber.value, $osRegistry.values.UBR.value
    }
    $report.os = [pscustomobject]@{
        cim = $osCim; registry = $osRegistry; nativeArchitectureRegistry = $architectureRegistry
        fullBuildFromRegistry = $fullBuild
    }

    $windowsDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $systemDirectory = [System.IO.Path]::Combine($windowsDirectory, 'System32')
    if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
        # Sysnative bypasses WOW64 redirection so this is not the x86 DLL by accident.
        $systemDirectory = [System.IO.Path]::Combine($windowsDirectory, 'Sysnative')
    }
    $paths = @{
        'explorer.exe' = @([System.IO.Path]::Combine($windowsDirectory, 'explorer.exe'))
        'ExplorerFrame.dll' = @([System.IO.Path]::Combine($systemDirectory, 'ExplorerFrame.dll'))
        'FileExplorerExtensions.dll' = @([System.IO.Path]::Combine($systemDirectory, 'FileExplorerExtensions.dll'))
    }
    $processItems = @()
    $moduleDiscoveryIncomplete = $false
    try {
        $processes = @(Get-CimInstance -ClassName Win32_Process -Filter "Name = 'explorer.exe'" -Property ProcessId, SessionId, ExecutablePath)
        foreach ($process in $processes) {
            $modules = @()
            $moduleError = $null
            $moduleStatus = 'not_found'
            $processHandle = $null
            try {
                $processHandle = Get-Process -Id $process.ProcessId -ErrorAction Stop
                foreach ($module in $processHandle.Modules) {
                    if ($paths.ContainsKey($module.ModuleName)) {
                        $modules += [pscustomobject]@{ name = $module.ModuleName; path = $module.FileName }
                        $paths[$module.ModuleName] += $module.FileName
                    }
                }
                if ($modules.Count -gt 0) { $moduleStatus = 'found' }
            } catch {
                $moduleError = Get-ProbeFailure $_
                $moduleStatus = $moduleError.status
                $moduleDiscoveryIncomplete = $true
            } finally {
                if ($null -ne $processHandle) { $processHandle.Dispose() }
            }
            $executablePathStatus = 'unavailable'
            if (-not [string]::IsNullOrEmpty($process.ExecutablePath)) {
                $executablePathStatus = 'found'
                $paths['explorer.exe'] += $process.ExecutablePath
            }
            $processItems += [pscustomobject]@{
                processId = $process.ProcessId; sessionId = $process.SessionId
                executablePath = $process.ExecutablePath; executablePathStatus = $executablePathStatus
                moduleProbeStatus = $moduleStatus; moduleProbeError = $moduleError; modules = @($modules)
            }
        }
        $processStatus = 'not_found'
        if ($processItems.Count -gt 0) { $processStatus = 'found' }
        $report.explorerProcesses = [pscustomobject]@{ status = $processStatus; items = @($processItems) }
    } catch {
        $report.explorerProcesses = [pscustomobject]@{
            status = 'incomplete'; error = (Get-ProbeFailure $_); items = @($processItems)
        }
        $moduleDiscoveryIncomplete = $true
    }

    $systemAppsRoot = [System.IO.Path]::Combine($windowsDirectory, 'SystemApps')
    $scanErrors = @()
    $scanMatches = @()
    try {
        foreach ($directory in @(Get-ChildItem -LiteralPath $systemAppsRoot -Directory -Force)) {
            $candidate = [System.IO.Path]::Combine($directory.FullName, 'FileExplorerExtensions.dll')
            try {
                $item = Get-Item -LiteralPath $candidate -Force
                if (-not $item.PSIsContainer) {
                    $paths['FileExplorerExtensions.dll'] += $candidate
                    $scanMatches += $candidate
                }
            } catch {
                $failure = Get-ProbeFailure $_
                if ($failure.status -ne 'not_found') {
                    $scanErrors += [pscustomobject]@{ path = $candidate; error = $failure }
                }
            }
        }
    } catch { $scanErrors += [pscustomobject]@{ path = $systemAppsRoot; error = (Get-ProbeFailure $_) } }
    $scanStatus = 'not_found'
    if ($scanMatches.Count -gt 0) { $scanStatus = 'found' }
    elseif ($scanErrors.Count -gt 0) { $scanStatus = 'incomplete' }
    $artifactReports = [ordered]@{}
    foreach ($name in @('explorer.exe', 'ExplorerFrame.dll', 'FileExplorerExtensions.dll')) {
        $incomplete = $moduleDiscoveryIncomplete
        if ($name -eq 'FileExplorerExtensions.dll' -and $scanErrors.Count -gt 0) { $incomplete = $true }
        $artifactReports[$name] = Read-ArtifactCandidates -Paths $paths[$name] -DiscoveryIncomplete $incomplete
    }
    $report.files = [pscustomobject]@{
        artifacts = [pscustomobject]$artifactReports
        systemAppsProbe = [pscustomobject]@{
            status = $scanStatus; root = $systemAppsRoot; depth = 1
            matches = @($scanMatches); errors = @($scanErrors)
        }
    }
    if ($ProbeWindhawk) { $report.windhawk = Find-Windhawk }
} elseif ($ProbeWindhawk) {
    $report.windhawk = [pscustomobject]@{ status = 'unsupported_platform' }
}

$json = [pscustomobject]$report | ConvertTo-Json -Depth 16
if ($PSBoundParameters.ContainsKey('OutFile')) {
    if ([string]::IsNullOrWhiteSpace($OutFile)) { throw 'OutFile must not be empty.' }
    $outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutFile)
    $outputStream = $null
    $writer = $null
    try {
        $outputStream = [System.IO.File]::Open($outputPath, [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
        $writer = [System.IO.StreamWriter]::new($outputStream, [System.Text.UTF8Encoding]::new($false))
        $writer.WriteLine($json)
    } finally {
        if ($null -ne $writer) { $writer.Dispose() }
        elseif ($null -ne $outputStream) { $outputStream.Dispose() }
    }
}
Write-Output $json
