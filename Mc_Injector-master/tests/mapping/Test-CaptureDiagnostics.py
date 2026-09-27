import json, pathlib, subprocess, sys
build=pathlib.Path(sys.argv[1])
command=[str(build/'MappingAnalyzer.exe'),'inspect','--pid','1','--lite','--java',str(build/'missing-java.exe'),'--native-loader',str(build/'missing-loader.exe'),'--out',str(build/'missing-target.jsonl')]
run=subprocess.run(command,text=True,capture_output=True,encoding='utf-8')
assert run.returncode==1,run.stdout
records=[json.loads(line) for line in run.stdout.splitlines()]
paths=[r for r in records if r['event']=='CAPTURE_PATH'][-1]
for key in ('standardAttach','fallback'):
 assert paths[key]['status']=='failed' and not paths[key]['started'],paths
 assert paths[key]['reason'] and paths[key]['javaRuntime'].endswith('missing-java.exe'),paths
 assert 'stderr' in paths[key] and 'exitCode' in paths[key],paths
stats=[r for r in records if r['event']=='SNAPSHOT_STATS']
assert len(stats)==2 and all(not s['available'] and s['loadedClassCount'] is None for s in stats)
assert 'standard Attach failed:' in records[-1]['reason'] and 'fallback capture failed:' in records[-1]['reason']
print('Capture diagnostics: separate launch failures, Java runtime, stderr, exit codes and unavailable statistics passed')

lite=build/'mapping-fixture/lite-roundtrip.jsonl'
if lite.exists():
 for operation in ('validate','resolve'):
  args=[str(build/'MappingAnalyzer.exe'),operation,'--pack',str(build/'mappings/default-v1.json'),'--snapshot',str(lite),'--out',str(build/'lite-not-verified.json')]
  run=subprocess.run(args,text=True,capture_output=True,encoding='utf-8')
  assert run.returncode!=0 and 'inspect-detail' in run.stdout,run.stdout
 print('Lite diagnostic snapshots cannot validate or resolve into injection mappings: passed')
