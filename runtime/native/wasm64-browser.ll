target triple = "wasm64-unknown-unknown"

; The browser platform is implemented in wasm.inc, in the primitive runtime.
declare i64 @wd.sys_write(i64, ptr, i64)
declare i64 @wd.output_write(i64, ptr, i64)
declare void @wd.exit(i32) noreturn
declare i64 @wd_file_open(ptr, i64, i64)
declare i64 @wd_file_read(i64, ptr, i64)
declare i64 @wd_file_write(i64, ptr, i64)
declare i64 @wd_file_close(i64)
declare i64 @wd_file_seek(i64, i64, i64)
declare void @wd_m_args_init(i64, ptr)
