param([Parameter(Mandatory=$true)][string]$Build,[Parameter(Mandatory=$true)][string]$Jdk)
$ErrorActionPreference='Stop'
$fixture=Join-Path $Build 'mapping-fixture'
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
& "$Jdk/bin/javac.exe" -encoding UTF-8 -d $fixture "$PSScriptRoot/MappingCaptureFixture.java"
if($LASTEXITCODE){throw 'fixture compile failed'}
[IO.File]::WriteAllText((Join-Path $fixture 'MANIFEST.MF'),"Premain-Class: MappingCaptureFixture`n`n")
& "$Jdk/bin/jar.exe" cfm "$fixture/transformer.jar" "$fixture/MANIFEST.MF" -C $fixture MappingCaptureFixture.class -C $fixture 'MappingCaptureFixture$1.class'
if($LASTEXITCODE){throw 'fixture jar failed'}
$target=Start-Process -FilePath "$Jdk/bin/java.exe" -ArgumentList @('-javaagent:'+"$fixture/transformer.jar",'-cp',$fixture,'MappingCaptureFixture') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$fixture/target.log" -RedirectStandardError "$fixture/target-error.log"
try {
    Start-Sleep -Milliseconds 900
    & "$Build/MappingAnalyzer.exe" inspect --pid $target.Id --java "$Jdk/bin/java.exe" --helper "$Build/attach-helper/McOverlayAttachHelper.jar" --probe "$Build/MappingProbe.dll" --out "$fixture/snapshot.json"
    if($LASTEXITCODE){throw 'live inspect failed'}
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
