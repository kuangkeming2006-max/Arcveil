param([Parameter(Mandatory=$true)][string]$Build,[Parameter(Mandatory=$true)][string]$Jdk,[switch]$DisableAttach,[switch]$Lite)
$ErrorActionPreference='Stop'
$fixture=Join-Path $Build 'mapping-fixture'
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
& "$Jdk/bin/javac.exe" -encoding UTF-8 -d $fixture "$PSScriptRoot/MappingCaptureFixture.java"
if($LASTEXITCODE){throw 'fixture compile failed'}
[IO.File]::WriteAllText((Join-Path $fixture 'MANIFEST.MF'),"Premain-Class: MappingCaptureFixture`n`n")
& "$Jdk/bin/jar.exe" cfm "$fixture/transformer.jar" "$fixture/MANIFEST.MF" -C $fixture MappingCaptureFixture.class -C $fixture 'MappingCaptureFixture$1.class'
if($LASTEXITCODE){throw 'fixture jar failed'}
$javaArgs=@('-javaagent:'+"$fixture/transformer.jar",'-cp',$fixture,'MappingCaptureFixture')
if($DisableAttach){$javaArgs=@('-XX:+DisableAttachMechanism')+$javaArgs}
$target=Start-Process -FilePath "$Jdk/bin/java.exe" -ArgumentList $javaArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput "$fixture/target.log" -RedirectStandardError "$fixture/target-error.log"
try {
    Start-Sleep -Milliseconds 900
    $captureArgs=@('inspect','--pid',$target.Id,'--java',"$Jdk/bin/java.exe",'--helper',"$Build/attach-helper/McOverlayAttachHelper.jar",'--probe',"$Build/MappingProbe.dll",'--native-loader',"$Build/McOverlayNativeLoader.exe",'--out',"$fixture/snapshot.json")
    if($Lite){$captureArgs=$captureArgs[0..3]+$captureArgs[6..($captureArgs.Count-1)];$captureArgs+='--lite'}
    $events=& "$Build/MappingAnalyzer.exe" @captureArgs
    $events | Write-Output
    if($LASTEXITCODE){throw 'live inspect failed'}
    if($Lite){
        $parsed=@($events|ForEach-Object {$_|ConvertFrom-Json})
        $stats=@($parsed|Where-Object event -eq 'SNAPSHOT_STATS')[-1]
        $fingerprint=@($parsed|Where-Object event -eq 'fingerprint')[-1].fingerprint
        if(!$fingerprint -or $stats.constantPoolBytes -ne 0 -or $stats.bytecodeBytes -ne 0){throw 'lite fetched detailed data or fingerprint missing'}
        $again=& "$Build/MappingAnalyzer.exe" @captureArgs
        if($LASTEXITCODE){throw 'second lite capture failed'}
        $second=@($again|ForEach-Object {$_|ConvertFrom-Json}|Where-Object event -eq 'fingerprint')[-1].fingerprint
        if($fingerprint -ne $second){throw 'stable private JVM lite fingerprint changed'}
        & "$Build/MappingAnalyzer.exe" inspect --snapshot "$fixture/snapshot.json" --lite --out "$fixture/lite-roundtrip.jsonl"
        if($LASTEXITCODE){throw 'lite streamed roundtrip failed'}
        Write-Output 'Live lite capture: two stable fingerprints; zero constant pool/bytecode bytes; streamed roundtrip passed'
        return
    }
    $snapshot=Get-Content -Raw "$fixture/snapshot.json" | ConvertFrom-Json
    $subject=@($snapshot.classes | Where-Object name -eq 'LMappingCaptureSubject;')
    if($subject.Count -ne 1){throw 'captured subject missing/ambiguous'}
    $expected=([BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes('mapping-modified'))).Replace('-','').ToLowerInvariant()
    if(!$subject[0].constantPool.Contains($expected)){throw 'probe did not capture transformed constant pool'}
    if(!(Get-Content -Raw "$fixture/target.log").Contains('mapping-modified')){throw 'transformation did not execute'}
    & "$Build/MappingAnalyzer.exe" inspect --snapshot "$fixture/snapshot.json" --out "$fixture/roundtrip.json"
    if($LASTEXITCODE){throw 'standalone inspect failed'}
    Write-Output 'Live transformed capture: passed (installed JVM bytes, not disk class)'
} finally { if(!$target.HasExited){Stop-Process -Id $target.Id}; }
