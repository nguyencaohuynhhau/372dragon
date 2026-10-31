param(
    [Parameter(Mandatory=$true)][string]$Stage,
    [ValidateSet('Offline','WSLTest')][string]$Mode = 'Offline',
    [ValidateSet('None','Preview','Create','Select')][string]$CreateMode = 'None',
    [string]$OutDir = '',
    [string]$CiBin = 'Build\cmake\ci-x64\bin',
    [switch]$ValidateOnly,
    [ValidateRange(30,900)][int]$Seconds = 60,
    # CYS-RELOGIN: Select only; N times world -> ReturnServerLogin -> account screen -> world.
    [ValidateRange(0,5)][int]$SwitchCycles = 0
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..')).TrimEnd('\')
function InBuild([string]$path) {
    if (-not [IO.Path]::IsPathRooted($path)) { $path=Join-Path $root $path }
    $full=[IO.Path]::GetFullPath($path)
    if (-not $full.StartsWith((Join-Path $root 'Build')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Path must stay inside CY-Source Build.' }
    $cursor=$full
    while ($cursor) {
        $item=Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Do not traverse junctions.' }
        $cursor=Split-Path -Parent $cursor
    }
    return $full
}
function ChildPath([string]$base,[string]$relative) {
    if ([IO.Path]::IsPathRooted($relative)) { throw 'Manifest paths must be relative.' }
    $path=InBuild (Join-Path $base $relative)
    if (-not $path.StartsWith($base.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escaped its stage.' }
    return $path
}
function Hash([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function SavePriorLogs([string]$bin,[string]$OutDir) {
    $prior=ChildPath $OutDir 'prior-logs'
    if (Test-Path -LiteralPath $prior) { throw 'Prior-log backup already exists; never overwrite evidence.' }
    $planned=@(foreach ($name in @('WXClient.log','WXRender.log','UISystem.log','Ogre.log','WXC_SML.TXT')) {
        $source=ChildPath $bin $name
        $destination=ChildPath $prior $name
        if (Test-Path -LiteralPath $destination) { throw 'Prior-log destination already exists.' }
        if (Test-Path -LiteralPath $source) {
            $item=Get-Item -LiteralPath $source -Force
            if ($item.PSIsContainer) { throw 'Expected a regular prior log file.' }
            [ordered]@{name=$name;source=$source;backup=$destination;bytes=$item.Length;sha256=(Hash $source)}
        }
    })
    $manifestPath=ChildPath $prior 'manifest.json'
    [void][IO.Directory]::CreateDirectory($prior)
    foreach ($record in $planned) {
        # Resolve both paths again immediately before each native PowerShell move.
        $source=ChildPath $bin $record.name
        $destination=ChildPath $prior $record.name
        if (Test-Path -LiteralPath $destination) { throw 'Prior-log destination already exists.' }
        if ((Hash $source) -ne $record.sha256) { throw 'Prior log changed before it could be preserved.' }
        Move-Item -LiteralPath $source -Destination $destination
        if ((Hash $destination) -ne $record.sha256) { throw 'Prior-log backup hash differs.' }
    }
    $journal=[ordered]@{created=(Get-Date).ToString('o');files=$planned}
    [IO.File]::WriteAllText($manifestPath,($journal|ConvertTo-Json -Depth 5),(New-Object Text.UTF8Encoding($false)))
}
function ExactSetting([string]$text,[string]$name,[string]$expected) {
    $values=[regex]::Matches($text,'(?m)^\s*'+[regex]::Escape($name)+'\s*=\s*([^\r\n]*)')
    if ($values.Count -ne 1 -or $values[0].Groups[1].Value.Trim() -ne $expected) { throw ('Unexpected or duplicate setting: '+$name) }
}
$Stage=InBuild $Stage
$CiBin=InBuild $CiBin
if (-not $CiBin.StartsWith((Join-Path $root 'Build\cmake')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'CiBin must be a repository CMake build output.' }
if (-not $OutDir) { $OutDir='Build\Logs\game-gl-login\'+$Mode+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fffffff') }
$OutDir=InBuild $OutDir
if (Test-Path -LiteralPath $OutDir) { throw 'Choose a new output path; preserve prior evidence.' }
$bin=Join-Path $Stage 'Bin64'
$manifest=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $bin 'offline_stage.json') | ConvertFrom-Json
foreach ($file in $manifest.files) {
    if ([IO.Path]::GetFileName($file.name) -ne $file.name) { throw 'Invalid binary manifest filename.' }
    $path=ChildPath $bin $file.name
    if ((Hash $path) -ne $file.sha256.ToLowerInvariant()) { throw ('Staged binary differs: '+$file.name) }
    if ($file.kind -eq 'ours' -and (Hash (Join-Path $CiBin $file.name)) -ne (Hash $path)) { throw ('Staged binary is not the selected CI output: '+$file.name) }
}
foreach ($name in @('Game.exe','UI_CEGUI.dll','CEGUIBase.dll','CEGUIFalagard.dll','OgreMain.dll')) {
    if (@($manifest.files | Where-Object { $_.name -eq $name -and $_.kind -eq 'ours' }).Count -ne 1) { throw ('Required CI binary absent: '+$name) }
}
if (@(Get-ChildItem -LiteralPath $bin -Filter '*.exe' | Where-Object Name -ne 'Game.exe').Count) { throw 'Unexpected helper executable in the stage.' }
$profile=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $bin 'gl_login_profile.json') | ConvertFrom-Json
if ($profile.applied -ne $true) { throw 'GL profile application did not finish.' }
foreach ($file in $profile.files) {
    $path=ChildPath $Stage $file.path
    if ((Hash $path) -ne $file.sha256.ToLowerInvariant()) { throw ('GL profile file differs: '+$file.path) }
}
$pairs=@('Logon/LogOn.layout.xml','Logon/LogOn.lua','SelectServer/SelectServer.layout.xml','SelectServer/SelectServer.lua','SoftKeyBoard/SoftKeyBoard.layout.xml','SoftKeyBoard/SoftKeyBoard.lua')
$pairRecords=@(foreach ($relative in $pairs) {
    $canonical=Join-Path (Join-Path $root 'Assets\Canonical\Data\InterfaceFolder') $relative
    $staged=ChildPath $Stage ('Data/Interface/'+$relative)
    $sha=Hash $canonical
    if ((Hash $staged) -ne $sha) { throw ('Staged GL pair differs from Canonical: '+$relative) }
    $record=@($profile.pairs | Where-Object { $_.path -eq $relative -and $_.sha256 -eq $sha })
    if ($record.Count -ne 1) { throw ('GL pair missing from applied manifest: '+$relative) }
    [ordered]@{path=$relative;sha256=$sha}
})
$font=ChildPath $Stage 'Data/Interface/Schema/NotoSansSC-Regular.ttf'
if ((Hash $font) -ne (Hash (Join-Path $root 'Assets/Canonical/Data/InterfaceFolder/Schema/NotoSansSC-Regular.ttf'))) { throw 'Staged Noto font differs from Canonical.' }
$creationProfile=$null
if ($SwitchCycles -gt 0 -and $CreateMode -ne 'Select') { throw 'SwitchCycles requires -CreateMode Select.' }
if ($CreateMode -ne 'None') {
    if (($CreateMode -eq 'Preview' -and $Mode -ne 'Offline') -or ($CreateMode -in @('Create','Select') -and $Mode -ne 'WSLTest')) { throw 'Preview requires Offline; Create/Select require WSLTest.' }
    if ($CreateMode -in @('Preview','Create') -and $Seconds -lt 180) { throw 'Creation smoke requires at least 180 seconds for rendered frames and server replies.' }
    if ($SwitchCycles -gt 0 -and $Seconds -lt 180*(1+$SwitchCycles)) { throw 'Switch cycles require at least 180 seconds per login.' }
    $creationProfile=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $bin 'create_role_profile.json') | ConvertFrom-Json
    if ($creationProfile.applied -ne $true -or $creationProfile.source -ne '../Tools/DATA NEW/DataCY31-08-26' -or $null -eq $creationProfile.appearance) { throw 'A complete applied DATA CY creation profile is required.' }
    if ($creationProfile.font_codepage -ne 65001 -or $creationProfile.strfilter_rules -ne 0 -or $creationProfile.font_sha256 -ne (Hash $font)) { throw 'Creation font/filter settings differ.' }
    $creationPaths=@{}
    foreach ($file in $creationProfile.files) {
        if ($creationPaths.ContainsKey($file.path)) { throw 'Duplicate creation manifest target.' }
        $creationPaths[$file.path]=$true
        if ((Hash (ChildPath $Stage $file.path)) -ne $file.sha256.ToLowerInvariant()) { throw ('Creation profile file differs: '+$file.path) }
    }
    foreach ($name in @('Race','Model','Face','Hair','Portrait','Outfit','Visual','Camera')) {
        if (-not $creationPaths.ContainsKey('Data/Config/CysCreate'+$name+'.txt')) { throw ('Creation table missing from applied manifest: '+$name) }
    }
    foreach ($relative in @('Data/Interface/CreateRole/CreateRole.layout.xml','Data/Interface/CreateRole/CreateRole.lua','Data/Interface/SelectRole/SelectRole.layout.xml','Data/Interface/SelectRole/SelectRole.lua')) {
        if (-not $creationPaths.ContainsKey($relative)) { throw ('Creation controller/layout missing from manifest: '+$relative) }
    }
}
$port=if ($Mode -eq 'WSLTest') { '15733' } else { '14400' }
$cfg=[IO.File]::ReadAllText((Join-Path $bin 'WXSystem.cfg'))
ExactSetting $cfg 'GameServer_Address' '127.0.0.1'
ExactSetting $cfg 'GameServer_Port' $port
ExactSetting $cfg 'GameServer_ConnectDirect' '0'
ExactSetting $cfg 'System_CodePage' '65001'
$legacy=[IO.File]::ReadAllText((Join-Path $Stage 'Patch\WXLoginServer.txt'))
$legacyServers=[regex]::Matches($legacy,'(?ms)^\[LOGIN\d+\]\s*(.*?)(?=^\[|\z)')
if ($legacyServers.Count -ne 1) { throw 'Exactly one legacy fallback server is required.' }
ExactSetting $legacyServers[0].Groups[1].Value 'IP' '127.0.0.1'
ExactSetting $legacyServers[0].Groups[1].Value 'PORT' $port
$inside=$false; $begun=$false; $ended=$false; $rows=0
foreach ($line in [IO.File]::ReadAllLines((Join-Path $Stage 'Patch\LoginServer.txt'),(New-Object Text.UTF8Encoding($false,$true)))) {
    $text=$line.TrimStart()
    if ($text -eq 'SERVER_BEGIN') { if ($begun) { throw 'Duplicate server section.' };$inside=$true;$begun=$true;continue }
    if ($text -eq 'SERVER_END') { if (-not $inside) { throw 'Unexpected server section end.' };$inside=$false;$ended=$true;continue }
    if (-not $inside -or -not $text -or $text.StartsWith('#')) { continue }
    $fields=$text.Split(',')
    if ($fields.Count -notin @(14,15)) { throw 'Invalid modern login row.' }
    foreach ($index in 8..11) {
        if (($index -eq 8 -or $fields[$index]) -and $fields[$index] -ne ('127.0.0.1:'+$port)) { throw 'Every login endpoint must match the selected isolated mode.' }
    }
    if ($fields[12] -notin @('0','1') -or $fields[4] -notin @('0','1','2','3')) { throw 'A normal, available server is required.' }
    $rows++
}
if ($rows -ne 1 -or -not $begun -or -not $ended -or $inside) { throw 'Exactly one complete modern login server is required.' }
if ($ValidateOnly) { Write-Host ('PASS GL stage validation '+$Mode+' creation='+$CreateMode+'; no Game process started.'); exit 0 }
[void][IO.Directory]::CreateDirectory($OutDir)
$tmp=Join-Path $OutDir 'tmp';[void][IO.Directory]::CreateDirectory($tmp)
$game=Join-Path $bin 'Game.exe'
$arguments='-fl -cys-gl-login-smoke'
$before=@(Get-ChildItem -LiteralPath (Join-Path $Stage 'ScreenShots') -Filter 'cys-gl-*.png' -ErrorAction SilentlyContinue | ForEach-Object Name)
$started=Get-Date;$connections=New-Object 'System.Collections.Generic.List[object]'
$terminated=$false;$process=$null;$processStarted=$false;$launchError=$null
$info=New-Object Diagnostics.ProcessStartInfo
$info.FileName=$game;$info.Arguments=$arguments;$info.WorkingDirectory=$bin
$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$info.EnvironmentVariables['CYS_GL_LOGIN_SMOKE']='1'
$info.EnvironmentVariables.Remove('CYS_CREATE_ROLE_SMOKE')
if ($CreateMode -ne 'None') { $info.EnvironmentVariables['CYS_CREATE_ROLE_SMOKE']=$CreateMode }
$info.EnvironmentVariables.Remove('CYS_SWITCH_CYCLES')
if ($SwitchCycles -gt 0) { $info.EnvironmentVariables['CYS_SWITCH_CYCLES']=[string]$SwitchCycles }
$info.EnvironmentVariables['TEMP']=$tmp;$info.EnvironmentVariables['TMP']=$tmp
if ($Mode -eq 'WSLTest') {
    if (-not $info.EnvironmentVariables.ContainsKey('CYS_BOT_PWD')) { throw 'WSLTest requires the bounded test password in the process environment.' }
    $info.EnvironmentVariables['CYS_WSL_LOGIN_TEST']='1'
} else {
    $info.EnvironmentVariables.Remove('CYS_BOT_PWD');$info.EnvironmentVariables.Remove('CYS_WSL_LOGIN_TEST')
}
# Only the child environment carries the secret; no CLI/JSON/password reads.
Remove-Item Env:CYS_BOT_PWD -ErrorAction SilentlyContinue
try {
    SavePriorLogs $bin $OutDir
    $process=New-Object Diagnostics.Process;$process.StartInfo=$info
    if (-not $process.Start()) { throw 'Could not start the CI Game.' }
    $processStarted=$true
    $info.EnvironmentVariables.Remove('CYS_BOT_PWD')
    while (-not $process.HasExited -and (Get-Date) -lt $started.AddSeconds($Seconds)) {
        Start-Sleep -Seconds 2;$process.Refresh()
        if ($process.HasExited) { break }
        foreach ($socket in @(Get-NetTCPConnection -OwningProcess $process.Id -ErrorAction SilentlyContinue)) {
            $connections.Add([ordered]@{remote=$socket.RemoteAddress;port=$socket.RemotePort;state=[string]$socket.State})
            if ($socket.RemoteAddress -notin @('0.0.0.0','::') -and
                ($Mode -eq 'Offline' -or $socket.RemoteAddress -ne '127.0.0.1' -or $socket.RemotePort -ne [int]$port)) {
                throw 'Unexpected connection from the isolated Game.'
            }
        }
    }
} catch { $launchError=$_.Exception.Message } finally {
    $info.EnvironmentVariables.Remove('CYS_BOT_PWD')
    if ($processStarted -and -not $process.HasExited) { $process.Kill();$process.WaitForExit();$terminated=$true }
    foreach ($name in @('WXClient.log','WXRender.log','UISystem.log','Ogre.log','WXC_SML.TXT')) {
        $path=Join-Path $bin $name
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $OutDir }
    }
    $shots=@(Get-ChildItem -LiteralPath (Join-Path $Stage 'ScreenShots') -Filter 'cys-gl-*.png' -ErrorAction SilentlyContinue | Where-Object { $_.Name -notin $before })
    $records=@(foreach ($shot in $shots) {
        Copy-Item -LiteralPath $shot.FullName -Destination $OutDir
        [ordered]@{name=$shot.Name;sha256=(Hash $shot.FullName);bytes=$shot.Length}
    })
    $run=[ordered]@{sha=$manifest.sha;mode=$Mode;create_mode=$CreateMode;switch_cycles=$SwitchCycles;started=$started.ToString('o');finished=(Get-Date).ToString('o');command=$game+' '+$arguments;cwd=$bin;ci_bin=$CiBin;terminated_after_timeout=$terminated;pid=$null;exit_code=$null;shots=$records;sockets=$connections.ToArray();gl_pairs=$pairRecords;font_sha256=(Hash $font);creation_profile_sha256=$null;launch_error=$launchError}
    if ($creationProfile) { $run.creation_profile_sha256=Hash (Join-Path $bin 'create_role_profile.json') }
    if ($processStarted) { $run.pid=$process.Id;$run.exit_code=$process.ExitCode }
    if ($process) { $process.Dispose() }
    [IO.File]::WriteAllText((Join-Path $OutDir 'run.json'),($run|ConvertTo-Json -Depth 7),(New-Object Text.UTF8Encoding($false)))
}
if ($launchError) { throw $launchError }
& (Join-Path $PSScriptRoot 'validate_gl_login_result.ps1') -OutDir $OutDir
