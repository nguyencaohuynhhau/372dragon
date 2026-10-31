param([Parameter(Mandatory=$true)][string]$OutDir)
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
$OutDir=InBuild $OutDir
$run=Get-Content -Raw -Encoding UTF8 -LiteralPath (ChildPath $OutDir 'run.json') | ConvertFrom-Json
if ($run.mode -notin @('Offline','WSLTest') -or $run.create_mode -notin @('None','Preview','Create','Select')) { throw 'Invalid recorded smoke mode.' }
if ($run.terminated_after_timeout -isnot [bool] -or $null -eq $run.shots) { throw 'Incomplete recorded smoke metadata.' }
if ($run.launch_error) { throw $run.launch_error }
$Mode=$run.mode
$CreateMode=$run.create_mode
$terminated=$run.terminated_after_timeout
$records=@($run.shots)
# CYS-RELOGIN: run.json of older runs has no switch_cycles; every login marker below repeats once per relogin.
$cycles=if ($null -ne $run.switch_cycles) { [int]$run.switch_cycles } else { 0 }
if ($cycles -lt 0 -or $cycles -gt 5 -or ($cycles -gt 0 -and $CreateMode -ne 'Select')) { throw 'Invalid recorded switch cycles.' }
$logins=1+$cycles
foreach ($name in @('WXClient.log','UISystem.log','WXRender.log')) { [void](ChildPath $OutDir $name) }
$log=[IO.File]::ReadAllText((Join-Path $OutDir 'WXClient.log'))
$marker=if ($Mode -eq 'Offline') { 'PASS offline input; no authentication' } else { 'PASS WSL authenticated character selection' }
if ($CreateMode -eq 'Preview') { $marker='PASS Preview; no authentication or character submit' }
if ($CreateMode -eq 'Create') { $marker='PASS Create; WSL character persisted and selected' }
if ($CreateMode -eq 'Select') { $marker='PASS Select; WSL existing character loaded and selected' }
$required=@('Vietnamese/Chinese WM_CHAR roundtrip passed','click/tab/enter focus passed','soft keyboard typed/backspace/drag passed',$marker)
if ($CreateMode -in @('Preview','Create')) {
    $required+=@('CYS create smoke: female face/hair/outfit/portrait controls passed','CYS create smoke: male face/hair/outfit/portrait controls passed','CYS create smoke: original CY portrait grid and both pages passed','CYS create smoke: held rotation changed native actor','CYS create smoke: both original zoom controls changed native camera','CYS create smoke: all 15 CY sect buttons, both sexes and cancel passed','CYS create smoke: random CY appearance/name passed','CYS create smoke: Vietnamese/Chinese name WM_CHAR roundtrip passed','CYS create smoke: Back/reopen reset CY choices and retained usable input')
}
if (-not $terminated -or $log.Contains('fatal exception') -or $log.Contains('CYS Lua failure')) { throw ('Game did not survive the integration gate: '+$OutDir) }
foreach ($value in $required) { if (-not $log.Contains($value)) { throw ('Missing integration result: '+$value) } }
$uiLog=[IO.File]::ReadAllText((Join-Path $OutDir 'UISystem.log'))
# Relogin runs enter the world, whose CYS diagnostics also log (Error); ui_error_check (below) excludes those.
if ($log -match 'missing host function|LuaApiGen missing calls begin total=[1-9]' -or ($cycles -eq 0 -and $uiLog -match '\(Error\)')) { throw 'Runtime has missing hosts or UI errors; inspect preserved logs.' }
if ($CreateMode -ne 'None') {
    $renderLog=[IO.File]::ReadAllText((Join-Path $OutDir 'WXRender.log'))
    if ($renderLog -match 'Error at line|Error in material|Unable to compile|Failed to compile|no supported techniques') { throw 'Creation rendering has material or shader errors; inspect preserved logs.' }
}
$listMarker=if ($CreateMode -eq 'Select') { 'CYS WSL login: charlist count=1' } else { 'CYS WSL login: charlist count=0' }
if ($Mode -eq 'WSLTest' -and (-not $log.Contains('CYS WSL login: authenticated') -or -not $log.Contains($listMarker))) { throw 'The real WSL authentication/expected-list markers are missing.' }
if ($Mode -eq 'WSLTest') {
    $selectionMarker=if ($CreateMode -eq 'Select') { 'CYS create smoke: existing character selection visible; login/wait hidden' } else { 'CYS GL smoke: empty character selection visible; login/wait hidden' }
    foreach ($value in @($selectionMarker,'CYS GL smoke: submitted once through GL TextAccepted','CYS WSL login: started','CYS WSL login: authenticated',$listMarker)) {
        if ([regex]::Matches($log,[regex]::Escape($value)).Count -ne $logins) { throw 'Expected exactly one WSL login sequence per login.' }
    }
}
if ($Mode -eq 'Offline' -and $log.Contains('CYS WSL login: started')) { throw 'Offline test unexpectedly started authentication.' }
if ($CreateMode -eq 'Preview') {
    if ($log.Contains('CYS WSL create: submitted once from UI') -or $log.Contains('CYS create smoke: clicked Create once')) { throw 'Preview unexpectedly submitted a character.' }
    if (-not $log.Contains('CYS create smoke: offline empty selection fixture; no authentication') -or
        [regex]::Matches($log,[regex]::Escape('CYS create UI: offline permission fixture, no network')).Count -ne 2) { throw 'Expected the explicit offline fixture for initial open and reopen.' }
}
if ($CreateMode -eq 'Create') {
    foreach ($value in @('CYS create smoke: clicked Create once for CYTest00','CYS WSL create: submitted once from UI','CYS WSL create: result=0','CYS WSL login: charlist count=1','CYS WSL login: character selection opened count=1 created=1','CYS create smoke: typed server list count=1 name=CYTest00; native model and slot selected')) {
        if ([regex]::Matches($log,[regex]::Escape($value)).Count -ne 1) { throw ('Expected exactly one confirmed creation step: '+$value) }
    }
    if ([regex]::Matches($log,[regex]::Escape('CYS WSL create: permission=1')).Count -ne 2) { throw 'Expected two confirmed permission replies for initial open and reopen.' }
}
if ($CreateMode -eq 'Select') {
    if ($log.Contains('CYS WSL create:') -or $log.Contains('CYS create smoke: clicked Create once') -or $log.Contains('CYS create UI: offline permission fixture')) { throw 'Existing-character verification unexpectedly requested permission or creation.' }
    foreach ($value in @('CYS WSL login: character selection opened count=1 created=0','CYS create smoke: typed server list count=1 name=CYTest00; native model and slot selected')) {
        if ([regex]::Matches($log,[regex]::Escape($value)).Count -ne $logins) { throw ('Expected one fresh-session selection result per login: '+$value) }
    }
    foreach ($label in @('existing-character-selection-before','existing-character-selection')) {
        if (@($records | Where-Object { $_.name.EndsWith('-'+$label+'.png') -and $_.bytes -gt 0 }).Count -ne 1) { throw ('Existing character capture missing or duplicated: '+$label) }
    }
}
if ($CreateMode -in @('Preview','Create')) {
    foreach ($label in @('creation-empty-before','create-female-default','create-female-choices','create-male-default','create-male-choices','create-portrait-last','create-unicode','create-ready','create-back-selection','create-reopened')) {
        if (@($records | Where-Object { $_.name.EndsWith('-'+$label+'.png') -and $_.bytes -gt 0 }).Count -ne 1) { throw ('Creation capture missing or duplicated: '+$label) }
    }
    if ($CreateMode -eq 'Create' -and @($records | Where-Object { $_.name.EndsWith('-created-character-selection.png') -and $_.bytes -gt 0 }).Count -ne 1) { throw 'Created character capture missing.' }
}
if ($cycles -gt 0) {
    $perCycle=@('CYS relogin: ReturnServerLogin requested=1','CYS relogin: CGAskQuit sent; waiting for quit_complete','CYS relogin: visible_game_windows=0','CYS relogin: account prefilled=1','account screen, account prefilled, password empty')
    foreach ($value in $perCycle) {
        if ([regex]::Matches($log,[regex]::Escape($value)).Count -ne $cycles) { throw ('Expected one relogin result per cycle: '+$value) }
    }
    if ([regex]::Matches($log,'CYS relogin smoke: cycle=\d+ phase=in_world').Count -ne $cycles) { throw 'Expected phase=in_world once per cycle.' }
    if ([regex]::Matches($log,'CYS relogin: visible_game_windows=').Count -ne $cycles) { throw 'Game windows stayed visible after a return to login.' }
    if ($log.Contains('GE_NET_CLOSE')) { throw 'A relogin quit reached the NET_CLOSE message path.' }
    if (($log+"`n"+$uiLog) -match 'CYS-CRASHCEGUI[^\r\n]*reused=1') { throw 'CEGUI window reuse (CYS-CRASHCEGUI reused=1) after relogin.' }
    $since=([datetime]::Parse($run.started)).ToString('dd_HH:mm:ss')
    & python (Join-Path $root 'Tools\ui\ui_error_check.py') --ui (Join-Path $OutDir 'UISystem.log') --client (Join-Path $OutDir 'WXClient.log') --since $since --out (Join-Path $OutDir 'ui_error_check.json')
    if ($LASTEXITCODE -ne 0) { throw 'ui_error_check is not ok since the smoke started; see ui_error_check.json.' }
}
if ($records.Count -lt 3) { throw 'Integration captures are incomplete.' }
Write-Host ('PASS '+$Mode+' GL input/runtime creation='+$CreateMode+' switch_cycles='+$cycles+'; inspect captures in '+$OutDir)
