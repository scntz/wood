target triple = "x86_64-pc-windows-msvc"
; COFF's floating-use marker is data, not a CRT dependency.
@_fltused = global i32 0
@wd.crlf = private constant [2 x i8] c"\0D\0A"
declare dllimport ptr @GetStdHandle(i32)
declare dllimport i32 @WriteFile(ptr, ptr, i32, ptr, ptr)
declare dllimport void @ExitProcess(i32) noreturn
declare dllimport ptr @GetProcessHeap()
declare dllimport ptr @HeapAlloc(ptr, i32, i64)
declare dllimport i32 @HeapFree(ptr, i32, ptr)

define ptr @wd_platform_alloc(i64 %bytes) {
  %heap = call ptr @GetProcessHeap()
  %memory = call ptr @HeapAlloc(ptr %heap, i32 0, i64 %bytes)
  ret ptr %memory
}
define void @wd_platform_free(ptr %memory, i64 %bytes) {
  %heap = call ptr @GetProcessHeap()
  %ok = call i32 @HeapFree(ptr %heap, i32 0, ptr %memory)
  ret void
}

define i64 @wd.win_write(i64 %fd, ptr %data, i64 %size) {
  %output = icmp eq i64 %fd, 1
  %which = select i1 %output, i32 -11, i32 -12
  %handle = call ptr @GetStdHandle(i32 %which)
  %count = alloca i32
  store i32 0, ptr %count
  %length = trunc i64 %size to i32
  %ok = call i32 @WriteFile(ptr %handle, ptr %data, i32 %length, ptr %count, ptr null)
  %written = load i32, ptr %count
  %wide = zext i32 %written to i64
  %success = icmp ne i32 %ok, 0
  %result = select i1 %success, i64 %wide, i64 -1
  ret i64 %result
}

; Match production's Windows text-mode stdout/stderr: every LF becomes CRLF,
; including an LF already preceded by CR. Return logical input bytes consumed.
define i64 @wd.sys_write(i64 %fd, ptr %data, i64 %size) {
entry:
  br label %scan
scan:
  %i = phi i64 [ 0, %entry ], [ %next, %advance ]
  %end = icmp eq i64 %i, %size
  br i1 %end, label %span, label %byte
byte:
  %pointer = getelementptr i8, ptr %data, i64 %i
  %value = load i8, ptr %pointer
  %lf = icmp eq i8 %value, 10
  br i1 %lf, label %newline, label %advance
advance:
  %next = add i64 %i, 1
  br label %scan
newline:
  %first = icmp eq i64 %i, 0
  br i1 %first, label %translate, label %span
span:
  %count = call i64 @wd.win_write(i64 %fd, ptr %data, i64 %i)
  ret i64 %count
translate:
  br label %write
write:
  %offset = phi i64 [ 0, %translate ], [ %updated, %progress ]
  %remaining = sub i64 2, %offset
  %part = getelementptr i8, ptr @wd.crlf, i64 %offset
  %written = call i64 @wd.win_write(i64 %fd, ptr %part, i64 %remaining)
  %positive = icmp sgt i64 %written, 0
  %within = icmp ule i64 %written, %remaining
  %ok = and i1 %positive, %within
  br i1 %ok, label %progress, label %failure
progress:
  %updated = add i64 %offset, %written
  %done = icmp eq i64 %updated, 2
  br i1 %done, label %complete, label %write
complete:
  ret i64 1
failure:
  ret i64 -1
}

define i64 @wd.output_write(i64 %fd, ptr %data, i64 %size) {
  %result = call i64 @wd.sys_write(i64 %fd, ptr %data, i64 %size)
  ret i64 %result
}

define void @wd.exit(i32 %code) noreturn {
  call void @ExitProcess(i32 %code)
  unreachable
}

declare void @wd_m_stack_guard()
define void @mainCRTStartup() noreturn {
  call void @wd_m_stack_guard()
  %status = call i32 @wd_main()
  call void @ExitProcess(i32 %status)
  unreachable
}

; Private binary file handles use only Kernel32; never the CRT.
declare dllimport ptr @CreateFileW(ptr, i32, i32, ptr, i32, i32, ptr)
declare dllimport i32 @ReadFile(ptr, ptr, i32, ptr, ptr)
declare dllimport i32 @CloseHandle(ptr)
declare dllimport i32 @SetFilePointerEx(ptr, i64, ptr, i32)
declare dllimport i32 @GetLastError()
declare dllimport i32 @MultiByteToWideChar(i32, i32, ptr, i32, ptr, i32)

define i64 @wd.file_error() {
  %code = call i32 @GetLastError()
  switch i32 %code, label %invalid [
    i32 2, label %missing
    i32 3, label %missing
    i32 123, label %missing
    i32 206, label %missing
    i32 5, label %access
    i32 32, label %access
    i32 33, label %access
    i32 80, label %exists
    i32 183, label %exists
    i32 4, label %many
    i32 8, label %memory
    i32 14, label %memory
    i32 6, label %bad
    i32 112, label %full
  ]
missing:
  ret i64 -2
access:
  ret i64 -13
exists:
  ret i64 -17
many:
  ret i64 -24
memory:
  ret i64 -12
bad:
  ret i64 -9
full:
  ret i64 -28
invalid:
  ret i64 -22
}
define i64 @wd_file_open(ptr %path, i64 %size, i64 %mode) {
  %large = icmp sgt i64 %size, 2147483646
  br i1 %large, label %invalid, label %convert
invalid:
  ret i64 -22
convert:
  %n = trunc i64 %size to i32
  %length = call i32 @MultiByteToWideChar(i32 65001, i32 8, ptr %path, i32 %n, ptr null, i32 0)
  %ok = icmp sgt i32 %length, 0
  br i1 %ok, label %allocate, label %encoding
encoding:
  %empty = icmp eq i64 %size, 0
  %error = select i1 %empty, i64 -2, i64 -84
  ret i64 %error
allocate:
  %wide = zext i32 %length to i64
  %count = add i64 %wide, 1
  %bytes = shl i64 %count, 1
  %memory = call ptr @wd_platform_alloc(i64 %bytes)
  %null = icmp eq ptr %memory, null
  br i1 %null, label %oom, label %fill
oom:
  ret i64 -12
fill:
  %converted = call i32 @MultiByteToWideChar(i32 65001, i32 8, ptr %path, i32 %n, ptr %memory, i32 %length)
  %complete = icmp eq i32 %converted, %length
  br i1 %complete, label %open, label %conversion_failed
conversion_failed:
  call void @wd_platform_free(ptr %memory, i64 %bytes)
  ret i64 -84
open:
  %end = getelementptr i16, ptr %memory, i64 %wide
  store i16 0, ptr %end
  %r = icmp eq i64 %mode, 0
  %w = icmp eq i64 %mode, 1
  %a = icmp eq i64 %mode, 2
  %write = select i1 %a, i32 4, i32 1073741824
  %access = select i1 %r, i32 -2147483648, i32 %write
  %create = select i1 %a, i32 4, i32 1
  %truncate = select i1 %w, i32 2, i32 %create
  %disposition = select i1 %r, i32 3, i32 %truncate
  %handle = call ptr @CreateFileW(ptr %memory, i32 %access, i32 3, ptr null, i32 %disposition, i32 128, ptr null)
  ; Capture GetLastError before HeapFree can change it.
  %failure = call i64 @wd.file_error()
  call void @wd_platform_free(ptr %memory, i64 %bytes)
  %bad = icmp eq ptr %handle, inttoptr (i64 -1 to ptr)
  %number = ptrtoint ptr %handle to i64
  %result = select i1 %bad, i64 %failure, i64 %number
  ret i64 %result
}
define i64 @wd_file_read(i64 %fd, ptr %data, i64 %size) {
  %handle = inttoptr i64 %fd to ptr
  %n = trunc i64 %size to i32
  %count = alloca i32
  store i32 0, ptr %count
  %ok = call i32 @ReadFile(ptr %handle, ptr %data, i32 %n, ptr %count, ptr null)
  %failed = icmp eq i32 %ok, 0
  br i1 %failed, label %error, label %done
error:
  %code = call i64 @wd.file_error()
  ret i64 %code
done:
  %received = load i32, ptr %count
  %result = zext i32 %received to i64
  ret i64 %result
}
define i64 @wd_file_write(i64 %fd, ptr %data, i64 %size) {
  %handle = inttoptr i64 %fd to ptr
  %n = trunc i64 %size to i32
  %count = alloca i32
  store i32 0, ptr %count
  %ok = call i32 @WriteFile(ptr %handle, ptr %data, i32 %n, ptr %count, ptr null)
  %failed = icmp eq i32 %ok, 0
  br i1 %failed, label %error, label %done
error:
  %code = call i64 @wd.file_error()
  ret i64 %code
done:
  %written = load i32, ptr %count
  %result = zext i32 %written to i64
  ret i64 %result
}
define i64 @wd_file_close(i64 %fd) {
  %handle = inttoptr i64 %fd to ptr
  %ok = call i32 @CloseHandle(ptr %handle)
  %failed = icmp eq i32 %ok, 0
  br i1 %failed, label %error, label %done
error:
  %code = call i64 @wd.file_error()
  ret i64 %code
done:
  ret i64 0
}
define i64 @wd_file_seek(i64 %fd, i64 %offset, i64 %origin) {
  %handle = inttoptr i64 %fd to ptr
  %which = trunc i64 %origin to i32
  %position = alloca i64
  %ok = call i32 @SetFilePointerEx(ptr %handle, i64 %offset, ptr %position, i32 %which)
  %failed = icmp eq i32 %ok, 0
  br i1 %failed, label %error, label %done
error:
  %code = call i64 @wd.file_error()
  ret i64 %code
done:
  %result = load i64, ptr %position
  ret i64 %result
}

; Win64's compiler ABI passes allocation size in RAX. Probe every guard page,
; preserve RAX/RCX and leave stack allocation to the caller. No CRT helper.
define void @__chkstk() naked {
  call void asm sideeffect "pushq %rcx; pushq %rax; leaq 24(%rsp), %rcx; cmpq $$4096, %rax; jb 2f; 1: subq $$4096, %rcx; testb $$0, (%rcx); subq $$4096, %rax; cmpq $$4096, %rax; ja 1b; 2: subq %rax, %rcx; testb $$0, (%rcx); popq %rax; popq %rcx; retq", "~{memory}"()
  unreachable
}
