param([string]$Zig='zig')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$bin=Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Path $bin -Force | Out-Null
& "$root/build.ps1" -Zig $Zig
$objects=@(Get-ChildItem -LiteralPath "$root/build" -Filter '*.o' | ForEach-Object FullName)
& $Zig cc -target x86_64-windows-gnu -std=c++17 -O2 -fno-exceptions -fno-rtti -shared -DFAKELATENCY_TEST "$root/src/fakelatency.cpp" @objects -lkernel32 -lbcrypt -o "$bin/fakelatency-test.dll"
if($LASTEXITCODE){throw 'Test DLL build failed'}
& $Zig cc -target x86_64-windows-gnu -O2 -shared "$PSScriptRoot/fixture_text.cpp" "$PSScriptRoot/fixture_text.def" -o "$bin/tier0.dll"
if($LASTEXITCODE){throw 'Text fixture build failed'}
& $Zig cc -target x86_64-windows-gnu -O2 -shared "$PSScriptRoot/fixture_ack.cpp" -o "$bin/steamnetworkingsockets.dll"
if($LASTEXITCODE){throw 'ACK fixture build failed'}
& $Zig cc -target x86_64-windows-gnu -std=c++17 -O2 "$PSScriptRoot/test.cpp" -lkernel32 -o "$bin/test.exe"
if($LASTEXITCODE){throw 'Test runner build failed'}
Push-Location $bin
try { & "$bin/test.exe" "$bin/fakelatency-test.dll" "$root/fakelatency.dll"; if($LASTEXITCODE){throw 'Tests failed'} }
finally { Pop-Location }
