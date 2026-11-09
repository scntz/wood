param(
    [Parameter(Mandatory)][string]$BeforeWood,
    [Parameter(Mandatory)][string]$Wood,
    [Parameter(Mandatory)][string]$Output,
    [string]$Cxx = 'cl',
    [ValidateRange(1,20000)][int]$Count = 2000,
    [ValidateRange(1,100)][int]$Rounds = 6,
    [ValidateRange(1,31)][int]$Samples = 3,
    [ValidateNotNullOrEmpty()]
    [ValidateSet('join','replace','absent','csv','csv_plain','csv_dense','csv_wide','csv_empty')]
    [string[]]$Modes = @('join','replace','absent','csv','csv_plain','csv_dense','csv_wide','csv_empty'),
    [switch]$TimingOnly
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$outputPath=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $outputPath){throw 'Output exists; choose a new report path'}
$work=Join-Path (Split-Path $outputPath -Parent) ('string-buffers-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($work)|Out-Null
$Cxx=(Get-Command $Cxx).Source
$compilers=@{before=(Resolve-Path -LiteralPath $BeforeWood).Path;wood=(Resolve-Path -LiteralPath $Wood).Path}
$executables=@{}
$artifacts=@{}
$suffix=if($IsWindows){'.exe'}else{''}
$counter=if($TimingOnly){0}else{1}
foreach($kind in @('before','wood','cpp')){
    $source=Join-Path $PSScriptRoot 'reference_string_buffers.cpp'
    if($kind -ne 'cpp'){
        $source=Join-Path $work ($kind+'.cpp')
        & $compilers[$kind] emit-cpp (Join-Path $PSScriptRoot 'string_buffers.wd') -o $source
        if($LASTEXITCODE -ne 0){throw "Emission failed: $kind"}
        [IO.File]::WriteAllText($source,('#include "native_memory.hpp"'+"`n"+[IO.File]::ReadAllText($source)))
    }
    $exe=Join-Path $work ($kind+$suffix)
    if([IO.Path]::GetFileNameWithoutExtension($Cxx) -in @('cl','clang-cl')){
        $log=& $Cxx /nologo /std:c++latest /O2 /EHsc /utf-8 /permissive- /MT "/DWOOD_BENCH_MEMORY_COUNTERS=$counter" "/I$root/runtime" "/I$PSScriptRoot" "/Fe:$exe" "/Fo:$exe.obj" $source /link /INCREMENTAL:NO 2>&1|Out-String
    }else{
        $flags=@('-std=c++23','-O2',"-DWOOD_BENCH_MEMORY_COUNTERS=$counter","-I$root/runtime","-I$PSScriptRoot",$source,'-o',$exe,'-pthread')
        if($IsLinux){$flags+='-ldl'}
        if($IsWindows){$flags+='-lpsapi'}
        $log=& $Cxx @flags 2>&1|Out-String
    }
    $status=$LASTEXITCODE
    [IO.File]::WriteAllText((Join-Path $work ($kind+'-build.log')),$log)
    if($status -ne 0){throw "Build failed: $kind (see build log)"}
    $executables[$kind]=$exe
    $artifacts[$kind]=@{source_sha256=(Get-FileHash $source).Hash.ToLowerInvariant();executable_sha256=(Get-FileHash $exe).Hash.ToLowerInvariant()}
}
$measurements=[Collections.Generic.List[object]]::new()
foreach($mode in $Modes){
    $expectedHash='';$expectedChecksum=0
    for($sample=0;$sample -lt $Samples;$sample++){
        $order=if($sample%2){@('cpp','wood','before')}else{@('before','wood','cpp')}
        foreach($kind in $order){
            $file=Join-Path $work "$mode-$sample-$kind.txt"
            $raw=& $executables[$kind] $mode $Count $Rounds $file
            if($LASTEXITCODE -ne 0){throw "Run failed: $kind/$mode"}
            $data=$raw|ConvertFrom-Json
            $hash=(Get-FileHash $file).Hash.ToLowerInvariant()
            if($expectedHash -and ($expectedHash -ne $hash -or $expectedChecksum -ne $data.checksum)){throw "Full output/checksum mismatch: $kind/$mode"}
            $expectedHash=$hash;$expectedChecksum=$data.checksum
            if(($TimingOnly -and $data.allocations -ne -1) -or (-not $TimingOnly -and $data.allocations -lt 0)){throw 'Instrumentation mode differs'}
            $measurements.Add([ordered]@{mode=$mode;implementation=$kind;sample=$sample;count=$Count;rounds=$Rounds;checksum=$data.checksum;allocations=$data.allocations;allocated_bytes=$data.allocated_bytes;peak_bytes=$data.peak_bytes;process_peak_bytes=$data.process_peak_bytes;raw_elapsed_ns=$data.elapsed_ns;output_sha256=$hash})
        }
    }
}
$report=[ordered]@{schema=1;allocator=$(if($TimingOnly){'ordinary'}else{'instrumented global new'});timings_qualified=$false;timing_limit='This script does not qualify host load. Instrumented timings are not speed comparisons. Ordinary samples require independent quiet-host validation, warmups and distribution analysis before any performance claim.';cpp=$Cxx;compiler_artifacts=$artifacts;measurements=$measurements}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $outputPath -Encoding utf8NoBOM
Write-Output "All outputs matched; profile saved to $outputPath. Timings are unqualified."
