param([string]$Zig='zig')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$obj=Join-Path $root 'build'
New-Item -ItemType Directory -Path $obj -Force | Out-Null
$inputs=@('buffer','hook','trampoline','hde/hde64')
$objects=@()
foreach($name in $inputs){
    $output=Join-Path $obj (($name -replace '/','_')+'.o')
    & $Zig cc -target x86_64-windows-gnu -O2 -DNDEBUG -c (Join-Path "$root/third_party/minhook/src" "$name.c") -o $output
    if($LASTEXITCODE){throw 'Compile failed'}
    $objects+=$output
}
& $Zig cc -target x86_64-windows-gnu -std=c++17 -O2 -DNDEBUG -fno-exceptions -fno-rtti -shared -s (Join-Path $root 'src/fakelatency.cpp') @objects -lkernel32 -lbcrypt -o (Join-Path $root 'fakelatency.dll')
if($LASTEXITCODE){throw 'Link failed'}
Write-Output (Join-Path $root 'fakelatency.dll')
