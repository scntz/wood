param(
    [Parameter(Mandatory)][string]$Emitter,
    [Parameter(Mandatory)][string]$Stdlib,
    [Parameter(Mandatory)][string]$LLVMDirectory,
    [Parameter(Mandatory)][string]$Linker,
    [Parameter(Mandatory)][string]$TestDirectory,
    [Parameter(Mandatory)][string]$SourceDirectory,
    [Parameter(Mandatory)][string]$RuntimeObject,
    [string]$WslDistribution = '',
    [string]$Production = '',
    [string]$Cxx = '',
    [string]$Dumpbin = '',
    [switch]$BootstrapOnly,
    [switch]$CallablesOnly,
    [switch]$ArraysOnly,
    [switch]$FfiOnly,
    [switch]$MathOnly,
    [string]$NativeLibrary = ''
)
$ErrorActionPreference='Stop'
if($Dumpbin -eq ''){
    $besideLinker=Join-Path (Split-Path $Linker) 'dumpbin.exe'
    $Dumpbin=if(Test-Path -LiteralPath $besideLinker -PathType Leaf){$besideLinker}else{(Get-Command dumpbin.exe -ErrorAction Stop).Source}
}
if(([int]$BootstrapOnly.IsPresent+[int]$CallablesOnly.IsPresent+[int]$ArraysOnly.IsPresent+[int]$FfiOnly.IsPresent+[int]$MathOnly.IsPresent) -gt 1){throw 'Select one validation gate'}
$run=Join-Path $TestDirectory ('run-'+[guid]::NewGuid().ToString('N').Substring(0,12))
New-Item -ItemType Directory -Path $run -ErrorAction Stop | Out-Null
$commands=[Collections.Generic.List[object]]::new()
function Invoke-Checked([string]$Exe,[string[]]$Arguments,[string]$Label,[int]$Expected=0,[string]$WorkingDirectory=''){
    $info=[Diagnostics.ProcessStartInfo]::new($Exe)
    $info.UseShellExecute=$false
    $info.RedirectStandardOutput=$true
    $info.RedirectStandardError=$true
    $info.StandardOutputEncoding=[Text.UTF8Encoding]::new($false)
    $info.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
    if($WorkingDirectory -ne ''){$info.WorkingDirectory=$WorkingDirectory}
    foreach($arg in $Arguments){[void]$info.ArgumentList.Add($arg)}
    $process=[Diagnostics.Process]::new(); $process.StartInfo=$info
    [void]$process.Start()
    $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
    $timeout=if($BootstrapOnly){1800000}else{180000}
    if(-not $process.WaitForExit($timeout)){ $process.Kill($true); throw "Timed out: $Label" }
    $out=$stdout.GetAwaiter().GetResult(); $err=$stderr.GetAwaiter().GetResult(); $code=$process.ExitCode
    $commands.Add(@{exe=$Exe;arguments=$Arguments;exit_code=$code;label=$Label})
    [IO.File]::WriteAllText((Join-Path $run ($Label+'.log')),$out+$err,[Text.UTF8Encoding]::new($false))
    $process.Dispose()
    if($code -ne $Expected){throw "$Label exit $code, expected ${Expected}: $out$err"}
    return @{out=$out;err=$err;code=$code}
}
function Invoke-LLVM([string]$Tool,[string[]]$Arguments,[string]$Label){
    if($WslDistribution -ne ''){
        $mapped=@($Arguments | ForEach-Object {if($_ -match '^([A-Za-z]):[/\\]'){'/mnt/'+$Matches[1].ToLowerInvariant()+'/'+$_.Substring(3).Replace('\','/')}else{$_}})
        return Invoke-Checked 'wsl.exe' (@('-d',$WslDistribution,'--exec',($LLVMDirectory+'/'+$Tool))+$mapped) $Label
    }
    return Invoke-Checked (Join-Path $LLVMDirectory ($Tool+'.exe')) $Arguments $Label
}
function Build-Native([string]$Source,[string]$Label,[int]$Level){
    $ir=Join-Path $run ($Label+'.ll'); $object=Join-Path $run ($Label+'.obj'); $exe=Join-Path $run ($Label+'.exe')
    [void](Invoke-Checked $Emitter @($Stdlib,(Join-Path $SourceDirectory 'runtime/native'),'windows-x86_64',$Source.Replace('\','/'),$ir) ($Label+'-emit'))
    [void](Invoke-LLVM 'opt' @('-passes=verify','-disable-output',$ir) ($Label+'-verify'))
    $inputIR=$ir
    if($Level -eq 2){$inputIR=Join-Path $run ($Label+'-optimized.ll'); [void](Invoke-LLVM 'opt' @('-passes=default<O2>','-S',$ir,'-o',$inputIR) ($Label+'-opt'))}
    [void](Invoke-LLVM 'llc' @('-filetype=obj','-mtriple=x86_64-pc-windows-msvc',('-O='+$Level),$inputIR,'-o',$object) ($Label+'-codegen'))
    $libraries=if($FfiOnly){@($NativeLibrary)}else{@()}
    [void](Invoke-Checked $Linker (@('/nologo','/Brepro','/OPT:NOICF','/nodefaultlib','/entry:mainCRTStartup','/subsystem:console','/machine:x64','/stack:8388608',('/out:'+$exe),$object,$RuntimeObject)+$libraries+@('kernel32.lib')) ($Label+'-link'))
    $imports=Invoke-Checked $Dumpbin @('/imports',$exe) ($Label+'-imports')
    $dlls=@([regex]::Matches($imports.out,'(?im)^\s+(\S+\.dll)\s*$') | ForEach-Object {$_.Groups[1].Value.ToUpperInvariant()})
    if($dlls.Count -ne 1 -or $dlls[0] -ne 'KERNEL32.DLL'){throw "Unexpected native imports: $($dlls -join ', ')"}
    return $exe
}
if($FfiOnly){
    if($NativeLibrary -eq '' -or $Production -eq '' -or $Cxx -eq ''){throw 'FFI differential validation requires NativeLibrary, Production and Cxx'}
    foreach($level in @(0,2)){
        $app=Build-Native (Join-Path $SourceDirectory 'tests/native_ffi.wd') "ffi-O$level" $level
        $native=Invoke-Checked $app @() "ffi-O$level-run"
        $reference=Invoke-Checked $Production @((Join-Path $SourceDirectory 'tests/native_ffi.wd'),'--cxx',$Cxx,('-O'+$level),'--link-library',$NativeLibrary) "ffi-O$level-production"
        if($native.out -cne "native borrowed C ABI passed`r`n" -or $native.out -cne $reference.out -or $native.err -ne '' -or $reference.err -ne ''){throw 'Native/production FFI output differs'}
        $app=Build-Native (Join-Path $SourceDirectory 'tests/native_ffi_diagnostics.wd') "ffi-diagnostics-O$level" $level
        $native=Invoke-Checked $app @() "ffi-diagnostics-O$level-run"
        $reference=Invoke-Checked $Production @((Join-Path $SourceDirectory 'tests/native_ffi_diagnostics.wd'),'--cxx',$Cxx,('-O'+$level),'--link-library',$NativeLibrary) "ffi-diagnostics-O$level-production"
        if($native.out -cne $reference.out -or $native.err -ne '' -or $reference.err -ne ''){throw 'Native FFI error values/source positions differ'}
        foreach($case in @('function_identity','native_ffi_exit')){
            $expected=if($case -eq 'native_ffi_exit'){23}else{0}
            $app=Build-Native (Join-Path $SourceDirectory "tests/$case.wd") "$case-O$level" $level
            $native=Invoke-Checked $app @() "$case-O$level-run" $expected
            $reference=Invoke-Checked $Production @((Join-Path $SourceDirectory "tests/$case.wd"),'--cxx',$Cxx,('-O'+$level),'--link-library',$NativeLibrary) "$case-O$level-production" $expected
            if($native.out -cne $reference.out -or $native.err -ne '' -or $reference.err -ne ''){throw "Native $case differs"}
        }
    }
    $commands | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $run 'commands.json') -Encoding utf8
    [IO.File]::WriteAllText((Join-Path $run 'validation.txt'),'O0/O2 real C archive imports; byte strings, callbacks, reentrancy, deferred failures, COW numeric borrows and effectful rooted arguments; exact production differential output; Kernel32-only native imports.',[Text.UTF8Encoding]::new($false))
    Write-Output "Native Windows borrowed FFI validation passed: $run"
    return
}
if($MathOnly){
    if($Production -eq '' -or $Cxx -eq ''){throw 'Math differential validation requires Production and Cxx'}
    foreach($level in @(0,2)){
        $math=Build-Native (Join-Path $SourceDirectory 'tests/native_math.wd') "math-O$level" $level
        $text=Build-Native (Join-Path $SourceDirectory 'tests/native_text.wd') "text-O$level" $level
        $computing=Build-Native (Join-Path $SourceDirectory 'tests/native_computing.wd') "computing-O$level" $level
        $obj=Build-Native (Join-Path $SourceDirectory 'tests/obj_import.wd') "obj-O$level" $level
        [void](Invoke-Checked 'cmake' @("-DMATH_EXE=$math","-DTEXT_EXE=$text","-DCOMPUTING_EXE=$computing","-DOBJ_EXE=$obj","-DTEST_DIR=$run/data-O$level","-DSOURCE_DIR=$SourceDirectory","-DPRODUCTION=$Production","-DCXX=$Cxx","-DLEVEL=$level",'-P',(Join-Path $SourceDirectory 'tests/native/math-oracle.cmake')) "math-oracle-O$level")
    }
    $commands | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $run 'commands.json') -Encoding utf8
    [IO.File]::WriteAllText((Join-Path $run 'validation.txt'),'Eight Kernel32-only PE applications; full-range sin/cos oracle, read_text bytes/failures, unchanged numerical and OBJ-import corpus, exact production differential output at O0/O2.',[Text.UTF8Encoding]::new($false))
    Write-Output "Native Windows math/files validation passed: $run"
    return
}
if($ArraysOnly){
    if($Production -eq '' -or $Cxx -eq ''){throw 'Array differential validation requires Production and Cxx'}
    foreach($level in @(0,2)){
        $array=Build-Native (Join-Path $SourceDirectory 'tests/native_arrays.wd') "arrays-O$level" $level
        $errors=Build-Native (Join-Path $SourceDirectory 'tests/native_array_errors.wd') "errors-O$level" $level
        $diagnostics=Build-Native (Join-Path $SourceDirectory 'tests/native_array_diagnostics.wd') "diagnostics-O$level" $level
        $limit=Build-Native (Join-Path $SourceDirectory 'tests/native_array_limit.wd') "limit-O$level" $level
        $mesh=Build-Native (Join-Path $SourceDirectory 'examples/mesh_import.wd') "mesh-O$level" $level
        $verify=Build-Native (Join-Path $SourceDirectory 'tests/mesh_transform_verify.wd') "verify-O$level" $level
        [void](Invoke-Checked 'cmake' @("-DARRAY_EXE=$array","-DERROR_EXE=$errors","-DDIAGNOSTIC_EXE=$diagnostics","-DLIMIT_EXE=$limit","-DMESH_EXE=$mesh","-DVERIFY_EXE=$verify","-DTEST_DIR=$run/data-O$level","-DSOURCE_DIR=$SourceDirectory","-DPRODUCTION=$Production","-DCXX=$Cxx","-DLEVEL=$level",'-P',(Join-Path $SourceDirectory 'tests/native/array-oracle.cmake')) "array-oracle-O$level")
    }
    $commands | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $run 'commands.json') -Encoding utf8
    [IO.File]::WriteAllText((Join-Path $run 'validation.txt'),'Twelve Kernel32-only native PE programs; O0/O2 numerical success/failure, exact error values/positions, target-specific allocation limits, C++ differential results, unchanged OBJ import application and byte-identical OBJ outputs; native coordinate reimport and malformed-input preservation.',[Text.UTF8Encoding]::new($false))
    Write-Output "Native Windows array/numerical validation passed: $run"
    return
}
if($CallablesOnly){
    if($Production -eq '' -or $Cxx -eq ''){throw 'Callable differential validation requires Production and Cxx'}
    foreach($level in @(0,2)){
        foreach($case in @(
            @{name='native_callables';output="native callable ownership passed`r`n"},
            @{name='native_iteration';output="native iterator ownership passed`r`n"},
            @{name='native_stream_lines';output="native buffered file lines passed`r`n"},
            @{name='closures_iterators';output="native closures and lazy iterators passed`r`n"},
            @{name='binding_lifetimes';output="binding and iteration lifetimes passed`r`n"},
            @{name='functional';output="native generic algorithms passed`r`n"}
        )){
            $app=Build-Native (Join-Path $SourceDirectory "tests/$($case.name).wd") "$($case.name)-O$level" $level
            $result=Invoke-Checked $app @() "$($case.name)-O$level-run" 0 $run
            if($result.out -cne $case.output -or $result.err -ne ''){throw "Native $($case.name) differs"}
            $result=Invoke-Checked $Production @((Join-Path $SourceDirectory "tests/$($case.name).wd"),'--cxx',$Cxx) "$($case.name)-O$level-production" 0 $run
            if($result.out -cne $case.output -or $result.err -ne ''){throw "Production $($case.name) differs"}
        }
        $app=Build-Native (Join-Path $SourceDirectory 'examples/log_report.wd') "log-report-O$level" $level
        $data=Join-Path $run "report-O$level"
        New-Item -ItemType Directory -Path $data | Out-Null
        [void](Invoke-Checked 'cmake' @("-Dexecutable=$app","-DTEST_DIR=$data",'-P',(Join-Path $SourceDirectory 'tests/log_report_oracle.cmake')) "log-report-O$level-oracle")
    }
    $app=Join-Path $run 'production-report.exe'
    [void](Invoke-Checked $Production @('build',(Join-Path $SourceDirectory 'examples/log_report.wd'),'-o',$app,'--cxx',$Cxx) 'production-report-build')
    $data=Join-Path $run 'production-report-data'
    New-Item -ItemType Directory -Path $data | Out-Null
    [void](Invoke-Checked 'cmake' @("-Dexecutable=$app","-DTEST_DIR=$data",'-P',(Join-Path $SourceDirectory 'tests/log_report_oracle.cmake')) 'production-report-oracle')
    $commands | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $run 'commands.json') -Encoding utf8
    [IO.File]::WriteAllText((Join-Path $run 'validation.txt'),'Six differential suites at O0/O2; native and production log-report application oracles; fourteen native PE executables with Kernel32-only imports.',[Text.UTF8Encoding]::new($false))
    Write-Output "Native Windows callable/iterator validation passed: $run"
    return
}
if($BootstrapOnly){
    $sources=@((Get-ChildItem -LiteralPath (Join-Path $SourceDirectory 'bootstrap/src') -Filter '*.wd' -File).FullName)+@($Stdlib,$RuntimeObject,(Join-Path $SourceDirectory 'runtime/native/core.ll'),(Join-Path $SourceDirectory 'runtime/native/windows-x86_64.ll'))
    $frozen=@{}; foreach($source in $sources){$frozen[$source]=(Get-FileHash -LiteralPath $source).Hash}
    $frozen | ConvertTo-Json | Set-Content (Join-Path $run 'sources.json') -Encoding utf8
    foreach($stage in 1..3){
        $Emitter=Build-Native (Join-Path $SourceDirectory 'bootstrap/src/native_main.wd') "stage$stage" 0
        foreach($source in $sources){if((Get-FileHash -LiteralPath $source).Hash -cne $frozen[$source]){throw "Bootstrap input changed: $source"}}
    }
    foreach($extension in @('ll','exe')){
        $second=(Get-FileHash (Join-Path $run "stage2.$extension")).Hash
        $third=(Get-FileHash (Join-Path $run "stage3.$extension")).Hash
        if($second -cne $third){throw "Native Windows stage2/stage3 $extension files differ"}
    }
    $app=Build-Native (Join-Path $SourceDirectory 'tests/native_services.wd') 'services' 2
    $result=Invoke-Checked $app @() 'services-run'
    if($result.out -cne "native compiler services passed`r`n" -or $result.err -ne ''){throw 'Native Windows bootstrap service regression failed'}
    $commands | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $run 'commands.json') -Encoding utf8
    [IO.File]::WriteAllText((Join-Path $run 'validation.txt'),'Three native compiler rebuilds; stage2/stage3 IR and reproducible PE identical; Kernel32-only imports; rebuilt compiler compiled and ran O2 services.',[Text.UTF8Encoding]::new($false))
    Write-Output "Native Windows bootstrap fixed point passed: $run"
    return
}
$stack=Join-Path $run 'stack.wd'
$lines=[Collections.Generic.List[string]]::new()
$lines.Add('fn heavy(n: int) -> int:'); $lines.Add('    result = 0')
for($i=0;$i -lt 700;$i++){ $lines.Add("    x$i = n+$i"); $lines.Add("    result += x$i") }
$lines.Add('    return result'); $lines.Add('print(heavy(3))')
[IO.File]::WriteAllText($stack,($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
$textSource=Join-Path $run 'text.wd'
[IO.File]::WriteAllText($textSource,'print("line\npair\r\nend")'+"`n",[Text.UTF8Encoding]::new($false))
$expectedText="line`r`npair`r`r`nend`r`n"
foreach($level in @(0,2)){
    $core=Build-Native (Join-Path $SourceDirectory 'tests/native_backend.wd') "core-O$level" $level
    $result=Invoke-Checked $core @() "core-O$level-run"
    if($result.out -ne "independent native core passed 55 true None`r`n" -or $result.err -ne ''){throw 'Native Windows core output differs'}
    foreach($case in @(
        @{name='managed';source='tests/native_managed.wd';output="native managed values passed 14 second`r`n"},
        @{name='ast';source='bootstrap/src/native_ast_main.wd';output="native compiler AST passed 1200 719400`r`n"},
        @{name='lexer';source='bootstrap/tests/test_lexer.wd';output="bootstrap lexer unit checks passed`r`n"},
        @{name='parser';source='bootstrap/tests/test_parser.wd';output="bootstrap parser unit checks passed`r`n"},
        @{name='comprehensions';source='tests/native_comprehensions.wd';output="native comprehensions passed`r`n"},
        @{name='services';source='tests/native_services.wd';output="native compiler services passed`r`n"},
        @{name='semantics';source='bootstrap/tests/test_semantics.wd';output="bootstrap semantic kernel unit checks passed`r`n"},
        @{name='json-error';source='tests/native_json_error.wd';output="native error JSON passed`r`n"},
        @{name='call-abi';source='tests/native_call_abi.wd';output="native aggregate calls passed`r`n"}
    )){
        $app=Build-Native (Join-Path $SourceDirectory $case.source) "$($case.name)-O$level" $level
        $result=Invoke-Checked $app @() "$($case.name)-O$level-run"
        if($result.out -ne $case.output -or $result.err -ne ''){throw "Native $($case.name) differs"}
    }
    $app=Build-Native (Join-Path $SourceDirectory 'tests/native_args.wd') "args-O$level" $level
    $withSpace=Join-Path $run "args with space O$level.exe"
    Copy-Item -LiteralPath $app -Destination $withSpace -ErrorAction Stop
    $result=Invoke-Checked $withSpace @('two words','','quote"slash\','😃') "args-O$level-run"
    if($result.out -cne ('["two words","","quote\"slash\\","😃"]'+"`r`n") -or $result.err -ne ''){throw 'Native Windows argument decoding differs'}
    $files=Join-Path $run "files-O$level"
    New-Item -ItemType Directory -Path $files -ErrorAction Stop | Out-Null
    [IO.File]::WriteAllText((Join-Path $files 'input.wd'),"fn square(x:int)->int:`n    return x*x`nprint(square(3))`n",[Text.UTF8Encoding]::new($false))
    $app=Build-Native (Join-Path $SourceDirectory 'bootstrap/src/native_file_main.wd') "file-O$level" $level
    $result=Invoke-Checked $app @() "file-O$level-run" 0 $files
    if($result.out -ne "native file frontend passed`r`n" -or $result.err -ne ''){throw 'Native file frontend differs'}
    if([IO.File]::ReadAllText((Join-Path $files 'parsed.txt')) -cne "2`n"){throw 'Binary file newline was translated'}
    if([Convert]::ToHexString([IO.File]::ReadAllBytes((Join-Path $files 'binary.bin'))) -ne '615A620A21'){throw 'Native binary bytes differ'}
    $mesh=Build-Native (Join-Path $SourceDirectory 'examples/native_grid_mesh.wd') "mesh-O$level" $level
    $result=Invoke-Checked $mesh @() "mesh-O$level-run"
    $vertices=0; $faces=0
    foreach($line in ($result.out -split '\r?\n')){
        if($line -match '^v (-?\d+) (-?\d+) (-?\d+)$'){
            $x=$vertices%25; $y=[int][Math]::Floor($vertices/25)
            $height=($x*$x+$y*$y)%17
            if($line -ne "v $($x*2-3) $($height*2) $($y*2+3)"){throw "Wrong vertex: $line"}
            $vertices++
        }elseif($line -match '^f (\d+) (\d+) (\d+)$'){
            $cell=[int][Math]::Floor($faces/2); $x=$cell%24; $y=[int][Math]::Floor($cell/24)
            $a=$y*25+$x+1; $b=$a+1; $c=$a+26; $d=$a+25
            $expected=if($faces%2 -eq 0){"f $a $b $c"}else{"f $a $c $d"}
            if($line -ne $expected){throw "Wrong face: $line"}
            $faces++
        }elseif($line -ne ''){throw "Unexpected OBJ line: $line"}
    }
    if($vertices -ne 625 -or $faces -ne 1152 -or $result.err -ne ''){throw 'Native Windows mesh invalid'}
    $heavy=Build-Native $stack "stack-O$level" $level
    $result=Invoke-Checked $heavy @() "stack-O$level-run"
    if($result.out -ne "246750`r`n"){throw 'Stack-probe regression failed'}
    $text=Build-Native $textSource "text-O$level" $level
    $result=Invoke-Checked $text @() "text-O$level-run"
    if($result.out -ne $expectedText -or $result.err -ne ''){throw 'Windows text-mode newline conversion differs'}
    $errorFile=Join-Path $run "error-O$level.wd"
    [IO.File]::WriteAllText($errorFile,"value=9223372036854775807`nvalue=value+1`n",[Text.UTF8Encoding]::new($false))
    $failure=Build-Native $errorFile "error-O$level" $level
    $result=Invoke-Checked $failure @() "error-O$level-run" 70
    $diagnostic=$result.err | ConvertFrom-Json
    if($diagnostic.code -ne 'R4001' -or $diagnostic.message -ne 'integer overflow in addition' -or $diagnostic.line -ne 2 -or $diagnostic.column -lt 1){throw 'Windows native diagnostic differs'}
    $invalidSource=Join-Path $run "invalid-O$level.wd"
    [IO.File]::WriteAllText($invalidSource,'raise byte_string(255)+" valid:😀"'+"`n",[Text.UTF8Encoding]::new($false))
    $invalid=Build-Native $invalidSource "invalid-O$level" $level
    $result=Invoke-Checked $invalid @() "invalid-O$level-run" 70
    $invalidDiagnostic=$result.err | ConvertFrom-Json
    if($invalidDiagnostic.code -ne 'R4010' -or $invalidDiagnostic.message -cne "$([char]0xfffd) valid:😀"){throw 'Invalid UTF-8 diagnostic differs'}
}
if($Production -ne ''){
    foreach($case in @('native_managed','native_comprehensions')){
        $result=Invoke-Checked $Production @((Join-Path $SourceDirectory "tests/$case.wd"),'--cxx',$Cxx) "production-$case"
        $expected=if($case -eq 'native_managed'){"native managed values passed 14 second`r`n"}else{"native comprehensions passed`r`n"}
        if($result.out -cne $expected -or $result.err -ne ''){throw "Production $case differs"}
    }
    $productionFiles=Join-Path $run 'production-files'
    New-Item -ItemType Directory -Path $productionFiles -ErrorAction Stop | Out-Null
    Copy-Item -LiteralPath (Join-Path $files 'input.wd') -Destination (Join-Path $productionFiles 'input.wd')
    $result=Invoke-Checked $Production @((Join-Path $SourceDirectory 'bootstrap/src/native_file_main.wd'),'--cxx',$Cxx) 'production-file' 0 $productionFiles
    if($result.out -cne "native file frontend passed`r`n" -or $result.err -ne ''){throw 'Production file frontend differs'}
    $result=Invoke-Checked $Production @((Join-Path $SourceDirectory 'tests/native_backend.wd'),'--cxx',$Cxx) 'production-core'
    if($result.out -ne "independent native core passed 55 true None`r`n"){throw 'Production core output differs'}
    $result=Invoke-Checked $Production @($textSource,'--cxx',$Cxx) 'production-text'
    if($result.out -ne $expectedText){throw 'Production text-mode baseline differs'}
}
$commands | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $run 'commands.json') -Encoding utf8
$comparison=if($Production -ne ''){'production core comparison passed'}else{'production comparison not requested'}
$nativeCount=@($commands | Where-Object {$_.label.EndsWith('-link')}).Count
[IO.File]::WriteAllText((Join-Path $run 'validation.txt'),("$nativeCount native PE executables: O0/O2 core, managed values, compiler AST, lexer/parser suites, comprehensions, file frontend, mesh, stack probes, text-mode output, runtime failures/UTF-8; Kernel32-only imports; "+$comparison),[Text.UTF8Encoding]::new($false))
Write-Output "Native Windows O0/O2 validation passed: $run"
