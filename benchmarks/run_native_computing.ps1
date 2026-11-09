param([Parameter(Mandatory)][string]$Wood,
      [Parameter(Mandatory)][string]$Output,
      [string]$BaselineCpp,
      [string]$HostedCpp,
      [string]$HostedBaselineCpp,
      [string]$Cxx='cl',
      [ValidateRange(3,31)][int]$Samples=9,
      [ValidateRange(4,1000000)][int]$Count=200000,
      [ValidateRange(4,384)][int]$MatrixSize=128,
      [ValidateRange(1,100)][int]$Rounds=12)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$Wood=(Resolve-Path -LiteralPath $Wood).Path
$Cxx=(Get-Command $Cxx).Source
$outputPath=[IO.Path]::GetFullPath($Output)
$directory=Join-Path (Split-Path $outputPath -Parent) ('computing-'+[Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($directory)|Out-Null
$extension=if($IsWindows){'.exe'}else{''}
$artifacts=[ordered]@{compiler=$Wood;host_compiler=$Cxx;runner=$PSCommandPath;wood_source=(Join-Path $PSScriptRoot 'native_computing.wd');cpp_source=(Join-Path $PSScriptRoot 'reference_native_computing.cpp');counter=(Join-Path $PSScriptRoot 'native_memory.hpp')}
$generated=Join-Path $directory 'wood.cpp'
& $Wood emit-cpp $artifacts.wood_source -o $generated
if($LASTEXITCODE -ne 0){throw 'Wood emission failed'}
$code='#include "native_memory.hpp"' + "`n" + [IO.File]::ReadAllText($generated)
[IO.File]::WriteAllText($generated,$code)
$pattern='std::move\((wd_v[0-9]+)\)'
$moveSites=[regex]::Matches($code,$pattern).Count
if($moveSites -lt 1){throw 'No owned local move sites found'}
$noMoves=Join-Path $directory 'no-moves.cpp'
[IO.File]::WriteAllText($noMoves,[regex]::Replace($code,$pattern,'$1'))
$inputs=[ordered]@{wood=$generated;cpp=$artifacts.cpp_source;no_moves=$noMoves}
if($BaselineCpp){$inputs.baseline=(Resolve-Path -LiteralPath $BaselineCpp).Path}
foreach($entry in @(@('hosted',$HostedCpp),@('hosted_before',$HostedBaselineCpp))){
    if(-not $entry[1]){continue}
    $source=(Resolve-Path -LiteralPath $entry[1]).Path
    $copy=Join-Path $directory ($entry[0]+'.cpp')
    [IO.File]::WriteAllText($copy,('#include "native_memory.hpp"'+"`n"+[IO.File]::ReadAllText($source)))
    $inputs[$entry[0]]=$copy
    $artifacts[$entry[0]+'_input']=$source
}
$executables=[ordered]@{}
foreach($kind in $inputs.Keys){
    $exe=Join-Path $directory "$kind$extension"
    if([IO.Path]::GetFileNameWithoutExtension($Cxx) -in @('cl','clang-cl')){
        $log=& $Cxx /nologo /std:c++latest /O2 /MD /EHsc /utf-8 /permissive- /Qvec-report:2 "/I$root/runtime" "/I$PSScriptRoot" "/Fe:$exe" "/Fo:$exe.obj" $inputs[$kind] /link /INCREMENTAL:NO 2>&1 | Out-String
    }else{
        $log=& $Cxx -std=c++23 -O2 "-I$root/runtime" "-I$PSScriptRoot" $inputs[$kind] -o $exe -pthread $(if($IsLinux){'-ldl'}) $(if($IsWindows){'-lpsapi'}) 2>&1 | Out-String
    }
    $status=$LASTEXITCODE
    $logPath=Join-Path $directory "$kind-compile.log"
    [IO.File]::WriteAllText($logPath,$log)
    if($status -ne 0){throw "Native compilation failed ($kind): $log"}
    $executables[$kind]=$exe
    $artifacts["${kind}_executable"]=$exe
    $artifacts["${kind}_generated"]=$inputs[$kind]
    $artifacts["${kind}_compiler_log"]=$logPath
}
function Residue-Sum([long]$size,[long]$period){
    $blocks=[Math]::Floor($size/$period);$rest=$size%$period
    return $blocks*$period*($period-1)/2+$rest*($rest-1)/2
}
function Expected([string]$mode,[long]$size){
    if($mode -eq 'blend'){return ((Residue-Sum $size 1024)*0.1875+(Residue-Sum $size 17)*0.0625)*$Rounds}
    if($mode -eq 'owned'){return ((Residue-Sum $size 1024)*0.09375+(Residue-Sum $size 17)*0.0625)*$Rounds}
    if($mode -eq 'affine'){return ((Residue-Sum $size 31)*0.109375+(Residue-Sum $size 17)*0.375+(Residue-Sum $size 7)*1.125)*$Rounds}
    [double]$result=0
    for($k=0;$k -lt $size;$k++){
        [long]$a=0;[long]$b=0
        for($i=0;$i -lt $size;$i++){$a+=($i+$k)%13;$b+=($k*3+$i)%11}
        $result+=$a*$b*0.03125
    }
    return $result*$Rounds
}
function Measure-Native([string]$kind,[string]$mode,[long]$size){
    $watch=[Diagnostics.Stopwatch]::StartNew()
    $text=& $executables[$kind] $mode $size $Rounds | Out-String
    $status=$LASTEXITCODE;$watch.Stop()
    if($status -ne 0){throw "Benchmark failed $kind/$mode ($status): $text"}
    $sample=$text|ConvertFrom-Json
    if($sample.checksum -ne (Expected $mode $size) -or $sample.elapsed_ns -le 0 -or $sample.process_peak_bytes -le 0){throw "Invalid measurement $kind/$mode : $text"}
    $sample|Add-Member process_ms $watch.Elapsed.TotalMilliseconds
    return $sample
}
$raw=[Collections.Generic.List[object]]::new()
foreach($mode in @('blend','affine','matmul','owned')){
    $size=if($mode -eq 'matmul'){$MatrixSize}else{$Count}
    $kinds=@($executables.Keys|Where-Object{$_ -ne 'baseline' -or $mode -ne 'owned'})
    foreach($kind in $kinds){1..2|ForEach-Object{Measure-Native $kind $mode $size|Out-Null}}
    for($index=0;$index -lt $Samples;$index++){
        $order=@($kinds);if($index%2){[array]::Reverse($order)}
        foreach($kind in $order){
            $s=Measure-Native $kind $mode $size
            $raw.Add([ordered]@{mode=$mode;implementation=$kind;sample=$index;size=$size;checksum=$s.checksum;elapsed_ns=$s.elapsed_ns;allocations=$s.allocations;allocated_bytes=$s.allocated_bytes;peak_bytes=$s.peak_bytes;process_peak_bytes=$s.process_peak_bytes;process_ms=$s.process_ms})
        }
    }
}
function Median($values){
    $sorted=@($values|Sort-Object {[double]$_})
    $middle=[int][Math]::Floor($sorted.Count/2)
    if($sorted.Count%2){return $sorted[$middle]}
    return ($sorted[$middle-1]+$sorted[$middle])/2
}
$summary=[ordered]@{}
foreach($mode in @('blend','affine','matmul','owned')){
    $row=[ordered]@{}
    foreach($kind in $executables.Keys){
        $selected=@($raw|Where-Object{$_.mode -eq $mode -and $_.implementation -eq $kind})
        if($selected.Count -eq 0){$row[$kind]=$null;continue}
        $times=@($selected|ForEach-Object{[long]$_['elapsed_ns']}|Sort-Object)
        foreach($metric in @('allocations','allocated_bytes','peak_bytes')){
            if(@($selected|ForEach-Object{$_[$metric]}|Select-Object -Unique).Count -ne 1){throw "Unstable memory metric $mode/$kind/$metric"}
        }
        $row[$kind]=[ordered]@{median_ns=(Median $times);minimum_ns=$times[0];maximum_ns=$times[-1];allocations=$selected[0].allocations;allocated_bytes=$selected[0].allocated_bytes;peak_bytes=$selected[0].peak_bytes;median_process_peak_bytes=(Median @($selected|ForEach-Object{[long]$_['process_peak_bytes']}))}
    }
    $summary[$mode]=$row
}
$hashes=[ordered]@{}
foreach($name in $artifacts.Keys){$hashes[$name]=(Get-FileHash -LiteralPath $artifacts[$name]).Hash.ToLowerInvariant()}
$runtime=[ordered]@{}
foreach($file in Get-ChildItem -LiteralPath (Join-Path $root 'runtime') -Filter '*.hpp'|Sort-Object Name){$runtime[$file.Name]=(Get-FileHash $file.FullName).Hash.ToLowerInvariant()}
[ordered]@{
    schema_version=1;date_utc=[DateTime]::UtcNow.ToString('o');count=$Count;matrix_size=$MatrixSize;rounds=$Rounds;samples=$Samples;warmups=2;last_use_move_sites=$moveSites
    os=[Runtime.InteropServices.RuntimeInformation]::OSDescription;architecture=[Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString();processor=$env:PROCESSOR_IDENTIFIER
    processor_model=$(if($IsWindows){([Microsoft.Win32.Registry]::GetValue('HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0','ProcessorNameString','unknown')).Trim()}else{$env:PROCESSOR_IDENTIFIER});logical_processors=[Environment]::ProcessorCount
    compiler_version=$(if($IsWindows){(Get-Item $Cxx).VersionInfo.FileVersion}else{& $Cxx --version|Select-Object -First 1})
    flags='C++23 /O2 /MD /EHsc /utf-8 /permissive- /Qvec-report:2 or -std=c++23 -O2; no LTO/fast-math; matching compiler/CRT'
    methodology='Owned finite inputs; shape-validated kernels; compensated sums in Wood/C++; dyadic exact independent checksum oracles. Two warmups, alternating sample order, no unchecked primary baseline. no_moves removes only generated moves of owned Wood locals/parameters. baseline is a frozen earlier emitted translation unit. Optional hosted/hosted_before consume ordinary uninstrumented emitted C++ and add the same allocation counter prelude.'
    memory='Global new/new[] request count/total bytes/peak live bytes during computation; includes STL alignment padding; excludes input setup, instrumentation headers and C allocator bookkeeping. Timing includes instrumentation overhead. OS process peak working set/RSS includes startup and input setup and is not resettable. Every sample is a fresh process.'
    summary=$summary;raw_samples=$raw;artifacts=$artifacts;sha256=$hashes;runtime_sha256=$runtime
}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $outputPath -Encoding utf8NoBOM
Write-Output "Verified native computing measurements: $outputPath"
$summary|ConvertTo-Json -Depth 6
