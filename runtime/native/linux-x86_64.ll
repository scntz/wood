target triple = "x86_64-unknown-linux-gnu"

; Allocation is private to the freestanding managed primitive runtime.
define ptr @wd_platform_alloc(i64 %bytes) {
  %result = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},{rdx},{r10},{r8},{r9},~{rcx},~{r11},~{memory}"(i64 9, i64 0, i64 %bytes, i64 3, i64 34, i64 -1, i64 0)
  %error = icmp uge i64 %result, -4095
  %pointer = inttoptr i64 %result to ptr
  %value = select i1 %error, ptr null, ptr %pointer
  ret ptr %value
}
define void @wd_platform_free(ptr %memory, i64 %bytes) {
  %result = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},~{rcx},~{r11},~{memory}"(i64 11, ptr %memory, i64 %bytes)
  ret void
}

define i64 @wd.sys_write(i64 %fd, ptr %data, i64 %size) {
  %result = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11},~{memory}"(i64 1, i64 %fd, ptr %data, i64 %size)
  ret i64 %result
}

; SIGPIPE is blocked per thread, consumed only if this write generated it, and
; the exact previous mask is restored. Never change a process-wide disposition.
define i64 @wd.output_write(i64 %fd, ptr %data, i64 %size) {
entry:
  %mask = alloca i64
  %old = alloca i64
  %pending = alloca i64
  %zero = alloca [2 x i64]
  store i64 4096, ptr %mask
  store [2 x i64] zeroinitializer, ptr %zero
  %blocked = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},{rdx},{r10},~{rcx},~{r11},~{memory}"(i64 14, i64 0, ptr %mask, ptr %old, i64 8)
  %blocked_ok = icmp eq i64 %blocked, 0
  br i1 %blocked_ok, label %query, label %block_failed
block_failed:
  ret i64 -1
query:
  %queried = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},~{rcx},~{r11},~{memory}"(i64 127, ptr %pending, i64 8)
  %query_ok = icmp eq i64 %queried, 0
  br i1 %query_ok, label %write, label %query_failed
query_failed:
  br label %restore
write:
  %signals = load i64, ptr %pending
  %bit = and i64 %signals, 4096
  %was_pending = icmp ne i64 %bit, 0
  %result = call i64 @wd.sys_write(i64 %fd, ptr %data, i64 %size)
  %broken = icmp eq i64 %result, -32
  %new_signal = xor i1 %was_pending, true
  %consume = and i1 %broken, %new_signal
  br i1 %consume, label %wait, label %restore
wait:
  %waited = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},{rdx},{r10},~{rcx},~{r11},~{memory}"(i64 128, ptr %mask, ptr null, ptr %zero, i64 8)
  %interrupted = icmp eq i64 %waited, -4
  br i1 %interrupted, label %wait, label %restore
restore:
  %value = phi i64 [ -1, %query_failed ], [ %result, %write ], [ %result, %wait ]
  %restored = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},{rdx},{r10},~{rcx},~{r11},~{memory}"(i64 14, i64 2, ptr %old, ptr null, i64 8)
  %restore_ok = icmp eq i64 %restored, 0
  %final = select i1 %restore_ok, i64 %value, i64 -1
  ret i64 %final
}

define void @wd.exit(i32 %code) noreturn {
  %wide = zext i32 %code to i64
  call void asm sideeffect "syscall", "{rax},{rdi},~{rcx},~{r11},~{memory}"(i64 231, i64 %wide)
  unreachable
}

; Private binary file port. Open atomically sets close-on-exec (O_CLOEXEC).
define i64 @wd_file_open(ptr %path, i64 %size, i64 %mode) {
  %r = icmp eq i64 %mode, 0
  %w = icmp eq i64 %mode, 1
  %a = icmp eq i64 %mode, 2
  %create = select i1 %a, i64 1089, i64 193
  %write = select i1 %w, i64 577, i64 %create
  %access = select i1 %r, i64 0, i64 %write
  %flags = or i64 %access, 524288
  %result = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},{rdx},{r10},~{rcx},~{r11},~{memory}"(i64 257, i64 -100, ptr %path, i64 %flags, i64 438)
  ret i64 %result
}
define i64 @wd_file_read(i64 %fd, ptr %data, i64 %size) {
  %result = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11},~{memory}"(i64 0, i64 %fd, ptr %data, i64 %size)
  ret i64 %result
}
define i64 @wd_file_write(i64 %fd, ptr %data, i64 %size) {
  %result = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11},~{memory}"(i64 1, i64 %fd, ptr %data, i64 %size)
  ret i64 %result
}
define i64 @wd_file_close(i64 %fd) {
  %result = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},~{rcx},~{r11},~{memory}"(i64 3, i64 %fd)
  ret i64 %result
}
define i64 @wd_file_seek(i64 %fd, i64 %offset, i64 %origin) {
  %result = call i64 asm sideeffect "syscall", "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11},~{memory}"(i64 8, i64 %fd, i64 %offset, i64 %origin)
  ret i64 %result
}

; Kernel entry has no return address. Establish the SysV call alignment before
; entering ordinary LLVM functions; do not depend on a C startup object.
define void @_start() naked noreturn {
  call void asm sideeffect "xorq %rbp, %rbp; movq (%rsp), %rdi; leaq 8(%rsp), %rsi; andq $$-16, %rsp; callq wd_m_args_init; callq wd_main; movl %eax, %edi; movl $$231, %eax; syscall", "~{memory}"()
  unreachable
}
declare void @wd_m_args_init(i64, ptr)

; Used only by an explicitly selected optional-system-library application.
; libc initializes ELF TLS, the dynamic loader and pthread state before Wood.
; A freestanding application/compiling successor still enters the original
; _start above and does not link libc. No C/C++ compiler is used by either path.
declare extern_weak i32 @__libc_start_main(ptr, i32, ptr, ptr, ptr, ptr, ptr)
; Keep the weak dependency visible to LLVM at every optimization level. A
; symbol mentioned only inside inline assembly can lose its weak declaration
; during global DCE and accidentally become a strong static-link dependency.
define i32 @wd.host_start(ptr %main, i32 %argc, ptr %argv, ptr %init, ptr %fini, ptr %rtld, ptr %stack) {
  %status = call i32 @__libc_start_main(ptr %main, i32 %argc, ptr %argv, ptr %init, ptr %fini, ptr %rtld, ptr %stack)
  ret i32 %status
}
define i32 @wd.host_main(i32 %argc, ptr %argv, ptr %env) {
  %count = sext i32 %argc to i64
  call void @wd_m_args_init(i64 %count, ptr %argv)
  %status = call i32 @wd_main()
  ret i32 %status
}
define void @_start_hosted() naked noreturn {
  call void asm sideeffect "movq %rdx, %r9; xorq %rbp, %rbp; popq %rsi; movq %rsp, %rdx; andq $$-16, %rsp; pushq %rax; pushq %rsp; xorq %r8, %r8; xorq %rcx, %rcx; leaq wd.host_main(%rip), %rdi; callq wd.host_start; ud2", "~{memory}"()
  unreachable
}
