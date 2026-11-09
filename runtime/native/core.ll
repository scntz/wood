; Wood native core. No hosted C/C++ runtime or exception machinery.
; Platform files implement wd.sys_write and wd.exit using the OS ABI.
%wd.str = type { ptr, i64, ptr }
%wd.error = type { %wd.str, %wd.str, %wd.str, i64, i64 }
%wd.ctx = type { i1, %wd.error, %wd.str, i64, i64, i1, i32 }
%wd.seq = type { { i64, i64, ptr }, i64, i64, i64, ptr, ptr }
declare ptr @wd_m_array_borrow(ptr, ptr, i64)
declare double @wd_m_math_sin(ptr, double)
declare double @wd_m_math_cos(ptr, double)
declare void @wd_m_read_text(ptr, ptr, ptr)
declare i64 @wd_m_fs_size(ptr, ptr)
declare i64 @wd_m_fs_mkdirs(ptr, ptr)
declare i64 @wd_m_fs_remove(ptr, ptr)
declare void @wd_m_str_retain(ptr)
declare i64 @wd_m_map_remove(ptr,ptr,ptr)
declare i64 @wd_m_eof(ptr)
declare void @wd_m_read_line(ptr,ptr)
declare i64 @wd_m_time_ns(ptr)
declare double @wd_m_time_seconds(ptr)
declare void @wd_m_time_sleep(ptr,double)
declare void @wd_m_format_value(ptr,ptr,ptr,ptr,i64)
declare void @wd_m_threads_init()
declare ptr @wd_m_task_spawn(ptr,ptr,ptr)
declare ptr @wd_m_task_spawn_io(ptr,ptr,ptr,ptr,ptr,i64,i64)
declare ptr @wd_m_task_wait(ptr,ptr,i64)
declare void @wd_m_task_check(ptr,ptr)
declare i64 @wd_m_task_done(ptr,ptr)
declare void @wd_m_task_cancel(ptr,ptr)
declare void @wd_m_task_close(ptr)
declare i64 @wd_m_task_closed(ptr)
declare i64 @wd_m_task_cancelled()
declare void @wd_m_task_checkpoint(ptr)
declare void @wd_m_random_bytes(ptr, ptr, i64)
declare ptr @wd_m_db_open(ptr,ptr,i64)

; Additive optional CPython service ABI; ordinary programs never call these.
declare ptr @wd_m_py_import(ptr,ptr)
declare ptr @wd_m_py_attr(ptr,ptr,ptr)
declare ptr @wd_m_py_call(ptr,ptr,ptr,ptr)
declare ptr @wd_m_py_value(ptr,ptr)
declare ptr @wd_m_py_json(ptr,ptr)
declare ptr @wd_m_py_tuple(ptr,ptr)
declare ptr @wd_m_py_bytes(ptr,ptr)
declare void @wd_m_py_to_bytes(ptr,ptr,ptr)
declare void @wd_m_py_text(ptr,ptr,ptr)
declare i64 @wd_m_py_int(ptr,ptr)
declare double @wd_m_py_float(ptr,ptr)
declare i64 @wd_m_py_bool(ptr,ptr)
declare i64 @wd_m_py_closed(ptr,ptr)
declare void @wd_m_py_check(ptr,ptr)
declare void @wd_m_py_close(ptr,ptr)
declare void @wd_m_py_finish(ptr)

declare void @wd_m_db_check(ptr,ptr)
declare void @wd_m_db_close(ptr,ptr)
declare i64 @wd_m_db_closed(ptr,ptr)
declare void @wd_m_db_version(ptr,ptr)
declare ptr @wd_m_db_prepare(ptr,ptr,ptr)
declare ptr @wd_m_db_begin(ptr,ptr)
declare void @wd_m_db_commit(ptr,ptr)
declare void @wd_m_db_rollback(ptr,ptr)
declare i64 @wd_m_db_changes(ptr,ptr)
declare i64 @wd_m_db_last_id(ptr,ptr)
declare void @wd_m_stmt_check(ptr,ptr)
declare void @wd_m_stmt_close(ptr,ptr)
declare i64 @wd_m_stmt_step(ptr,ptr)
declare void @wd_m_stmt_reset(ptr,ptr)
declare void @wd_m_stmt_bind_int(ptr,ptr,i64,i64)
declare void @wd_m_stmt_bind_float(ptr,ptr,i64,double)
declare void @wd_m_stmt_bind_text(ptr,ptr,i64,ptr)
declare void @wd_m_stmt_bind_blob(ptr,ptr,i64,ptr)
declare void @wd_m_stmt_bind_null(ptr,ptr,i64)
declare i64 @wd_m_stmt_columns(ptr,ptr)
declare void @wd_m_stmt_name(ptr,ptr,ptr,i64)
declare void @wd_m_stmt_kind(ptr,ptr,ptr,i64)
declare i64 @wd_m_stmt_int(ptr,ptr,i64)
declare double @wd_m_stmt_float(ptr,ptr,i64)
declare void @wd_m_stmt_text(ptr,ptr,ptr,i64)
declare void @wd_m_stmt_blob(ptr,ptr,ptr,i64)
declare void @wd_m_transaction_check(ptr,ptr)
declare void @wd_m_sqlite_scope_close(ptr,ptr,i64)
declare ptr @wd_m_tls_context_new(ptr,ptr,ptr)
declare void @wd_m_tls_context_check(ptr,ptr)
declare void @wd_m_tls_context_close(ptr,ptr)
declare ptr @wd_m_tls_accept(ptr,ptr,ptr)
declare ptr @wd_m_tls_connect(ptr,ptr,i64,ptr,i64)
declare void @wd_m_tls_version(ptr,ptr)
declare void @wd_m_callback_push(ptr,ptr,ptr,ptr)
declare ptr @wd_m_callback_find(ptr)
declare void @wd_m_callback_pop(ptr)

; The private driver bridge owns an independent checked context. Its layout
; stays LLVM-owned; C code never guesses offsets or reinterprets a Wood error.
define i64 @wd_rt_context_size() {
  ret i64 ptrtoint (ptr getelementptr (%wd.ctx, ptr null, i64 1) to i64)
}
define void @wd_rt_context_error(ptr %ctx, ptr %code, ptr %message) {
  %ep = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 1
  %e = load %wd.error, ptr %ep
  %c = extractvalue %wd.error %e, 0
  %m = extractvalue %wd.error %e, 1
  store %wd.str %c, ptr %code
  store %wd.str %m, ptr %message
  call void @wd_m_str_retain(ptr %code)
  call void @wd_m_str_retain(ptr %message)
  ret void
}
define void @wd_rt_context_clear(ptr %ctx) {
  call void @wd.clear(ptr %ctx)
  ret void
}

; Additive pointer/scalar C ABI for resource cleanup on a task's own context.
define void @wd_rt_context_site(ptr %ctx,ptr %file,i64 %line,i64 %column) {
  %value = load %wd.str,ptr %file
  call void @wd.site(ptr %ctx,%wd.str %value,i64 %line,i64 %column)
  ret void
}

; Preserve original worker error locations/owned strings and explicit exits.
; The caller's current site is not substituted for a worker failure.
define void @wd_rt_context_forward(ptr %to,ptr %from) {
  %failed = call i1 @wd.failed(ptr %from)
  br i1 %failed,label %copy,label %exit
copy:
  %ep = getelementptr %wd.ctx,ptr %from,i32 0,i32 1
  %error = load %wd.error,ptr %ep
  call void @wd.set_error(ptr %to,%wd.error %error)
  br label %exit
exit:
  %exited = call i1 @wd.exited(ptr %from)
  br i1 %exited,label %request,label %end
request:
  %cp = getelementptr %wd.ctx,ptr %from,i32 0,i32 6
  %code = load i32,ptr %cp
  call void @wd.request_exit(ptr %to,i32 %code)
  br label %end
end:
  ret void
}

define void @wd.task_scope_close(ptr %ctx,ptr %slot,%wd.str %file,i64 %line,i64 %column) {
  %value = load ptr,ptr %slot
  call void @wd_m_task_close(ptr %value)
  ret void
}
@wd.hex = private constant [16 x i8] c"0123456789abcdef"
@wd.slash = private constant [1 x i8] c"\5C"
@wd.quotechar = private constant [1 x i8] c"\22"
@wd.prefix = private constant [27 x i8] c"{\22severity\22:\22error\22,\22code\22:"
@wd.filekey = private constant [8 x i8] c",\22file\22:"
@wd.linekey = private constant [8 x i8] c",\22line\22:"
@wd.colkey = private constant [10 x i8] c",\22column\22:"
@wd.msgkey = private constant [11 x i8] c",\22message\22:"
@wd.end = private constant [2 x i8] c"}\0A"
; wood build --diagnostics=json sets this to true in the program's IR text.
@wd.json_errors = private constant i1 false
@wd.colon = private constant [1 x i8] c":"
@wd.err_space = private constant [2 x i8] c": "
@wd.err_open = private constant [14 x i8] c"runtime error["
@wd.err_close = private constant [3 x i8] c"]: "
@wd.err_help = private constant [22 x i8] c"\0A  help: wood explain "
@wd.err_end = private constant [1 x i8] c"\0A"
@wd.leak = private constant [22 x i8] c"native ownership leak\0A"
@wd.invalid_utf8 = private constant [6 x i8] c"\5Cufffd"

define void @wd.check_memory() {
  call void @wd_native_runtime_abi4()
  %count = call i64 @wd_m_live_count()
  %ok = icmp eq i64 %count, 0
  br i1 %ok, label %end, label %failure
failure:
  call void @wd.write(i64 2, ptr @wd.leak, i64 22)
  call void @wd.exit(i32 71)
  unreachable
end:
  ret void
}

define void @wd.write(i64 %fd, ptr %data, i64 %size) {
entry:
  br label %loop
loop:
  %offset = phi i64 [ 0, %entry ], [ %next, %written ], [ %offset, %retry ]
  %done = icmp eq i64 %offset, %size
  br i1 %done, label %end, label %send
send:
  %remaining = sub i64 %size, %offset
  %large = icmp ugt i64 %remaining, 2147483647
  %chunk = select i1 %large, i64 2147483647, i64 %remaining
  %pointer = getelementptr i8, ptr %data, i64 %offset
  %count = call i64 @wd.sys_write(i64 %fd, ptr %pointer, i64 %chunk)
  %interrupted = icmp eq i64 %count, -4
  br i1 %interrupted, label %retry, label %check
retry:
  br label %loop
check:
  %positive = icmp sgt i64 %count, 0
  %within = icmp ule i64 %count, %chunk
  %ok = and i1 %positive, %within
  br i1 %ok, label %written, label %failure
written:
  %next = add i64 %offset, %count
  br label %loop
failure:
  call void @wd.exit(i32 70)
  unreachable
end:
  ret void
}

define void @wd.text(i64 %fd, %wd.str %value) {
  %data = extractvalue %wd.str %value, 0
  %size = extractvalue %wd.str %value, 1
  call void @wd.write(i64 %fd, ptr %data, i64 %size)
  ret void
}

@wd.output_code = private constant [5 x i8] c"R4008"
@wd.output_message = private constant [33 x i8] c"could not write to output stream\00"
; Public output is checked and unwinds through normal resource scopes. Diagnostic
; output continues to use the raw, allocation-free emergency write path above.
define void @wd.checked_text(ptr %ctx, i64 %fd, %wd.str %value) {
entry:
  %data = extractvalue %wd.str %value, 0
  %size = extractvalue %wd.str %value, 1
  br label %loop
loop:
  %offset = phi i64 [ 0, %entry ], [ %next, %written ], [ %offset, %retry ]
  %done = icmp eq i64 %offset, %size
  br i1 %done, label %end, label %send
send:
  %remaining = sub i64 %size, %offset
  %large = icmp ugt i64 %remaining, 2147483647
  %chunk = select i1 %large, i64 2147483647, i64 %remaining
  %pointer = getelementptr i8, ptr %data, i64 %offset
  %count = call i64 @wd.output_write(i64 %fd, ptr %pointer, i64 %chunk)
  %interrupted = icmp eq i64 %count, -4
  br i1 %interrupted, label %retry, label %check
retry:
  br label %loop
check:
  %positive = icmp sgt i64 %count, 0
  %within = icmp ule i64 %count, %chunk
  %ok = and i1 %positive, %within
  br i1 %ok, label %written, label %failure
written:
  %next = add i64 %offset, %count
  br label %loop
failure:
  call void @wd_rt_error(ptr %ctx, ptr @wd.output_code, i64 5, ptr @wd.output_message, i64 32)
  ret void
end:
  ret void
}

define void @wd.integer(i64 %fd, i64 %value) {
  call void @wd.integer_output(ptr null, i64 %fd, i64 %value)
  ret void
}
define void @wd.checked_integer(ptr %ctx, i64 %fd, i64 %value) {
  call void @wd.integer_output(ptr %ctx, i64 %fd, i64 %value)
  ret void
}
define internal void @wd.integer_output(ptr %ctx, i64 %fd, i64 %value) {
entry:
  %buffer = alloca [21 x i8]
  %negative = icmp slt i64 %value, 0
  %negated = sub i64 0, %value
  %magnitude = select i1 %negative, i64 %negated, i64 %value
  br label %digits
digits:
  %number = phi i64 [ %magnitude, %entry ], [ %quotient, %digits ]
  %position = phi i64 [ 21, %entry ], [ %previous, %digits ]
  %remainder = urem i64 %number, 10
  %quotient = udiv i64 %number, 10
  %previous = sub i64 %position, 1
  %digit64 = add i64 %remainder, 48
  %digit = trunc i64 %digit64 to i8
  %place = getelementptr i8, ptr %buffer, i64 %previous
  store i8 %digit, ptr %place
  %more = icmp ne i64 %quotient, 0
  br i1 %more, label %digits, label %sign
sign:
  br i1 %negative, label %minus, label %output
minus:
  %signed_start = sub i64 %previous, 1
  %signed_place = getelementptr i8, ptr %buffer, i64 %signed_start
  store i8 45, ptr %signed_place
  br label %output
output:
  %start = phi i64 [ %previous, %sign ], [ %signed_start, %minus ]
  %length = sub i64 21, %start
  %pointer = getelementptr i8, ptr %buffer, i64 %start
  %raw = icmp eq ptr %ctx, null
  br i1 %raw, label %emergency, label %checked
emergency:
  call void @wd.write(i64 %fd, ptr %pointer, i64 %length)
  ret void
checked:
  %span = insertvalue %wd.str zeroinitializer, ptr %pointer, 0
  %text = insertvalue %wd.str %span, i64 %length, 1
  call void @wd.checked_text(ptr %ctx, i64 %fd, %wd.str %text)
  ret void
}

define i32 @wd.compare(%wd.str %a, %wd.str %b) {
entry:
  %ap = extractvalue %wd.str %a, 0
  %an = extractvalue %wd.str %a, 1
  %bp = extractvalue %wd.str %b, 0
  %bn = extractvalue %wd.str %b, 1
  %shorter = icmp ult i64 %an, %bn
  %limit = select i1 %shorter, i64 %an, i64 %bn
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %equal ]
  %end = icmp eq i64 %i, %limit
  br i1 %end, label %lengths, label %bytes
bytes:
  %ax = getelementptr i8, ptr %ap, i64 %i
  %bx = getelementptr i8, ptr %bp, i64 %i
  %ac = load i8, ptr %ax
  %bc = load i8, ptr %bx
  %same = icmp eq i8 %ac, %bc
  br i1 %same, label %equal, label %different
equal:
  %next = add i64 %i, 1
  br label %loop
different:
  %less = icmp ult i8 %ac, %bc
  %order = select i1 %less, i32 -1, i32 1
  ret i32 %order
lengths:
  %same_length = icmp eq i64 %an, %bn
  %unequal = select i1 %shorter, i32 -1, i32 1
  %answer = select i1 %same_length, i32 0, i32 %unequal
  ret i32 %answer
}

define void @wd.quote(%wd.str %value) {
entry:
  %escape = alloca [6 x i8]
  %width = alloca i64
  %data = extractvalue %wd.str %value, 0
  %length = extractvalue %wd.str %value, 1
  call void @wd.write(i64 2, ptr @wd.quotechar, i64 1)
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %advance ]
  %end = icmp eq i64 %i, %length
  br i1 %end, label %finish, label %byte
byte:
  store i64 1, ptr %width
  %p = getelementptr i8, ptr %data, i64 %i
  %c = load i8, ptr %p
  %high = icmp uge i8 %c, 128
  br i1 %high, label %utf8, label %ascii
utf8:
  %remaining = sub i64 %length, %i
  %span = call i64 @wd_m_utf8_span(ptr %p, i64 %remaining)
  %valid = icmp sgt i64 %span, 0
  br i1 %valid, label %multibyte, label %invalid
multibyte:
  call void @wd.write(i64 2, ptr %p, i64 %span)
  store i64 %span, ptr %width
  br label %advance
invalid:
  call void @wd.write(i64 2, ptr @wd.invalid_utf8, i64 6)
  br label %advance
ascii:
  %quote = icmp eq i8 %c, 34
  %slash = icmp eq i8 %c, 92
  %special = or i1 %quote, %slash
  br i1 %special, label %escaped, label %control
escaped:
  call void @wd.write(i64 2, ptr @wd.slash, i64 1)
  br label %plain
control:
  %low = icmp ult i8 %c, 32
  br i1 %low, label %unicode, label %plain
unicode:
  store i8 92, ptr %escape
  %p1 = getelementptr i8, ptr %escape, i64 1
  store i8 117, ptr %p1
  %p2 = getelementptr i8, ptr %escape, i64 2
  store i8 48, ptr %p2
  %p3 = getelementptr i8, ptr %escape, i64 3
  store i8 48, ptr %p3
  %hi = lshr i8 %c, 4
  %lo = and i8 %c, 15
  %hi64 = zext i8 %hi to i64
  %lo64 = zext i8 %lo to i64
  %hp = getelementptr i8, ptr @wd.hex, i64 %hi64
  %lp = getelementptr i8, ptr @wd.hex, i64 %lo64
  %h = load i8, ptr %hp
  %l = load i8, ptr %lp
  %p4 = getelementptr i8, ptr %escape, i64 4
  %p5 = getelementptr i8, ptr %escape, i64 5
  store i8 %h, ptr %p4
  store i8 %l, ptr %p5
  call void @wd.write(i64 2, ptr %escape, i64 6)
  br label %advance
plain:
  call void @wd.write(i64 2, ptr %p, i64 1)
  br label %advance
advance:
  %step = load i64, ptr %width
  %next = add i64 %i, %step
  br label %loop
finish:
  call void @wd.write(i64 2, ptr @wd.quotechar, i64 1)
  ret void
}

define void @wd.fail(ptr %ctx, %wd.str %code, %wd.str %message, %wd.str %file, i64 %line, i64 %column) {
  %a = insertvalue %wd.error zeroinitializer, %wd.str %code, 0
  %b = insertvalue %wd.error %a, %wd.str %message, 1
  %c = insertvalue %wd.error %b, %wd.str %file, 2
  %d = insertvalue %wd.error %c, i64 %line, 3
  %e = insertvalue %wd.error %d, i64 %column, 4
  call void @wd.set_error(ptr %ctx, %wd.error %e)
  ret void
}

define void @wd.error_retain(%wd.error %e) {
  %a = extractvalue %wd.error %e, 0, 2
  %b = extractvalue %wd.error %e, 1, 2
  %c = extractvalue %wd.error %e, 2, 2
  call void @wd_m_retain(ptr %a)
  call void @wd_m_retain(ptr %b)
  call void @wd_m_retain(ptr %c)
  ret void
}
define void @wd.error_drop(%wd.error %e) {
  %a = extractvalue %wd.error %e, 0, 2
  %b = extractvalue %wd.error %e, 1, 2
  %c = extractvalue %wd.error %e, 2, 2
  call void @wd_m_release(ptr %a)
  call void @wd_m_release(ptr %b)
  call void @wd_m_release(ptr %c)
  ret void
}
define void @wd.clear(ptr %ctx) {
  %ep = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 1
  %old = load %wd.error, ptr %ep
  store %wd.error zeroinitializer, ptr %ep
  store i1 false, ptr %ctx
  call void @wd.error_drop(%wd.error %old)
  ret void
}
define void @wd.set_error(ptr %ctx, %wd.error %e) {
  call void @wd.error_retain(%wd.error %e)
  call void @wd.clear(ptr %ctx)
  %ep = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 1
  store %wd.error %e, ptr %ep
  store i1 true, ptr %ctx
  ret void
}
; Inline fast paths for loops over lists and arrays: a list's length, and whether a list, array or
; buffer has exactly one owner, so it can change in place. WSeq keeps its length at byte 24.
define i64 @wd.list_length(ptr %list) {
  %missing = icmp eq ptr %list, null
  br i1 %missing, label %empty, label %present
empty:
  ret i64 0
present:
  %at = getelementptr i8, ptr %list, i64 24
  %length = load i64, ptr %at
  ret i64 %length
}
define i1 @wd.exclusive(ptr %owner) {
  %refs = load atomic i64, ptr %owner acquire, align 8
  %one = icmp eq i64 %refs, 1
  ret i1 %one
}
define void @wd.site(ptr %ctx, %wd.str %file, i64 %line, i64 %column) {
  %fp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 2
  %lp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 3
  %cp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 4
  store %wd.str %file, ptr %fp
  store i64 %line, ptr %lp
  store i64 %column, ptr %cp
  ret void
}
define void @wd_rt_error(ptr %ctx, ptr %code, i64 %code_size, ptr %message, i64 %message_size) {
  %a = insertvalue %wd.str zeroinitializer, ptr %code, 0
  %b = insertvalue %wd.str %a, i64 %code_size, 1
  %c = insertvalue %wd.str zeroinitializer, ptr %message, 0
  %d = insertvalue %wd.str %c, i64 %message_size, 1
  %fp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 2
  %lp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 3
  %cp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 4
  %file = load %wd.str, ptr %fp
  %line = load i64, ptr %lp
  %col = load i64, ptr %cp
  call void @wd.fail(ptr %ctx, %wd.str %b, %wd.str %d, %wd.str %file, i64 %line, i64 %col)
  ret void
}
define void @wd.request_exit(ptr %ctx, i32 %code) {
  %flag = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 5
  %cp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 6
  store i1 true, ptr %flag
  store i32 %code, ptr %cp
  ret void
}
define i64 @wd_rt_unwinding(ptr %ctx) {
  %error = call i1 @wd.failed(ptr %ctx)
  %exit = call i1 @wd.exited(ptr %ctx)
  %busy = or i1 %error, %exit
  %wide = zext i1 %busy to i64
  ret i64 %wide
}

define void @wd_rt_error_value(ptr %ctx, ptr %code, i64 %code_size, ptr %message) {
  %a = insertvalue %wd.str zeroinitializer, ptr %code, 0
  %b = insertvalue %wd.str %a, i64 %code_size, 1
  %value = load %wd.str, ptr %message
  %fp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 2
  %lp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 3
  %cp = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 4
  %file = load %wd.str, ptr %fp
  %line = load i64, ptr %lp
  %col = load i64, ptr %cp
  call void @wd.fail(ptr %ctx, %wd.str %b, %wd.str %value, %wd.str %file, i64 %line, i64 %col)
  ret void
}

define void @wd.scope_close(ptr %ctx, ptr %slot, %wd.str %file, i64 %line, i64 %column) {
  %value = load ptr, ptr %slot
  %empty = icmp eq ptr %value, null
  br i1 %empty, label %end, label %close
close:
  call void @wd.site(ptr %ctx, %wd.str %file, i64 %line, i64 %column)
  call void @wd_m_stream_close(ptr %ctx, ptr %value, i64 1)
  br label %end
end:
  ret void
}
define i1 @wd.exited(ptr %ctx) {
  %flag = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 5
  %value = load i1, ptr %flag
  ret i1 %value
}

define i1 @wd.failed(ptr %ctx) {
  %failed = load i1, ptr %ctx
  ret i1 %failed
}

define void @wd.report(ptr %ctx) {
  %ep = getelementptr %wd.ctx, ptr %ctx, i32 0, i32 1
  %error = load %wd.error, ptr %ep
  %code = extractvalue %wd.error %error, 0
  %message = extractvalue %wd.error %error, 1
  %file = extractvalue %wd.error %error, 2
  %line = extractvalue %wd.error %error, 3
  %column = extractvalue %wd.error %error, 4
  %machine = load i1, ptr @wd.json_errors
  br i1 %machine, label %structured, label %readable
readable:
  %file_data = extractvalue %wd.str %file, 0
  %file_size = extractvalue %wd.str %file, 1
  %located = icmp ne i64 %file_size, 0
  br i1 %located, label %position, label %text
position:
  call void @wd.write(i64 2, ptr %file_data, i64 %file_size)
  call void @wd.write(i64 2, ptr @wd.colon, i64 1)
  call void @wd.integer(i64 2, i64 %line)
  call void @wd.write(i64 2, ptr @wd.colon, i64 1)
  call void @wd.integer(i64 2, i64 %column)
  call void @wd.write(i64 2, ptr @wd.err_space, i64 2)
  br label %text
text:
  %code_data = extractvalue %wd.str %code, 0
  %code_size = extractvalue %wd.str %code, 1
  %message_data = extractvalue %wd.str %message, 0
  %message_size = extractvalue %wd.str %message, 1
  call void @wd.write(i64 2, ptr @wd.err_open, i64 14)
  call void @wd.write(i64 2, ptr %code_data, i64 %code_size)
  call void @wd.write(i64 2, ptr @wd.err_close, i64 3)
  call void @wd.write(i64 2, ptr %message_data, i64 %message_size)
  call void @wd.write(i64 2, ptr @wd.err_help, i64 22)
  call void @wd.write(i64 2, ptr %code_data, i64 %code_size)
  call void @wd.write(i64 2, ptr @wd.err_end, i64 1)
  ret void
structured:
  call void @wd.write(i64 2, ptr @wd.prefix, i64 27)
  call void @wd.quote(%wd.str %code)
  call void @wd.write(i64 2, ptr @wd.filekey, i64 8)
  call void @wd.quote(%wd.str %file)
  call void @wd.write(i64 2, ptr @wd.linekey, i64 8)
  call void @wd.integer(i64 2, i64 %line)
  call void @wd.write(i64 2, ptr @wd.colkey, i64 10)
  call void @wd.integer(i64 2, i64 %column)
  call void @wd.write(i64 2, ptr @wd.msgkey, i64 11)
  call void @wd.quote(%wd.str %message)
  call void @wd.write(i64 2, ptr @wd.end, i64 2)
  ret void
}

; Freestanding compiler-generated aggregate copies must not introduce libc.
; optnone prevents loop-idiom recognition from replacing a routine with itself.
define ptr @memcpy(ptr %dest, ptr %source, i64 %n) noinline optnone {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %copy ]
  %done = icmp eq i64 %i, %n
  br i1 %done, label %end, label %copy
copy:
  %s = getelementptr i8, ptr %source, i64 %i
  %d = getelementptr i8, ptr %dest, i64 %i
  %v = load i8, ptr %s
  store i8 %v, ptr %d
  %next = add i64 %i, 1
  br label %loop
end:
  ret ptr %dest
}

define ptr @memmove(ptr %dest, ptr %source, i64 %n) noinline optnone {
entry:
  %before = icmp ult ptr %dest, %source
  br i1 %before, label %forward, label %backward
forward:
  %r = call ptr @memcpy(ptr %dest, ptr %source, i64 %n)
  ret ptr %r
backward:
  br label %loop
loop:
  %i = phi i64 [ %n, %backward ], [ %next, %copy ]
  %done = icmp eq i64 %i, 0
  br i1 %done, label %end, label %copy
copy:
  %next = sub i64 %i, 1
  %s = getelementptr i8, ptr %source, i64 %next
  %d = getelementptr i8, ptr %dest, i64 %next
  %v = load i8, ptr %s
  store i8 %v, ptr %d
  br label %loop
end:
  ret ptr %dest
}

define ptr @memset(ptr %dest, i32 %value, i64 %n) noinline optnone {
entry:
  %v = trunc i32 %value to i8
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %copy ]
  %done = icmp eq i64 %i, %n
  br i1 %done, label %end, label %copy
copy:
  %d = getelementptr i8, ptr %dest, i64 %i
  store i8 %v, ptr %d
  %next = add i64 %i, 1
  br label %loop
end:
  ret ptr %dest
}

declare {i64, i1} @llvm.sadd.with.overflow.i64(i64, i64)
declare {i64, i1} @llvm.ssub.with.overflow.i64(i64, i64)
declare {i64, i1} @llvm.smul.with.overflow.i64(i64, i64)

; All managed primitive boundaries use pointers/scalars, never C aggregates.
declare void @wd_m_retain(ptr)
declare void @wd_m_release(ptr)
declare i64 @wd_m_live_count()
declare i64 @wd_m_live_bytes()
declare i64 @wd_m_peak_bytes()
declare void @wd_m_str_concat(ptr, ptr, ptr, ptr)
declare void @wd_m_str_repeat(ptr, ptr, ptr, i64)
declare void @wd_m_str_slice(ptr, ptr, ptr, i64, i64, i64, i64, i64)
declare void @wd_m_str_replace(ptr, ptr, ptr, ptr, ptr)
declare i64 @wd_m_find(ptr, ptr)
declare void @wd_m_int_str(ptr, ptr, i64)
declare i64 @wd_m_str_int(ptr, ptr)
declare double @wd_m_str_float(ptr, ptr)
declare void @wd_m_float_str(ptr, ptr, double)
declare ptr @wd_m_json_convert(ptr, ptr, ptr, i64)
declare ptr @wd_m_json_parse(ptr, ptr)
declare void @wd_m_json_dump(ptr, ptr, ptr)
declare void @wd_m_json_kind(ptr, ptr, ptr)
declare void @wd_m_json_str(ptr, ptr, ptr)
declare ptr @wd_m_json_list(ptr, ptr)
declare ptr @wd_m_json_dict(ptr, ptr)
declare i64 @wd_m_json_int(ptr, ptr)
declare i64 @wd_m_json_bool(ptr, ptr)
declare double @wd_m_json_float(ptr, ptr)
declare i64 @wd_m_json_equal(ptr, ptr)
declare ptr @wd_m_args(ptr)
declare void @wd_m_write_text(ptr, ptr, ptr)
declare void @wd_m_path_join(ptr, ptr, ptr, ptr)
declare void @wd_m_path_name(ptr, ptr, ptr)
declare void @wd_m_path_parent(ptr, ptr, ptr)
declare void @wd_m_path_normalize(ptr, ptr, ptr)
declare void @wd_m_path_absolute(ptr, ptr, ptr)
declare void @wd_m_path_resolve(ptr, ptr, ptr, i64)
declare void @wd_m_path_identity(ptr, ptr, ptr)
declare i64 @wd_m_path_is_absolute(ptr, ptr)
declare i64 @wd_m_path_exists(ptr, ptr)
declare i64 @wd_m_path_within(ptr, ptr, ptr)
declare void @wd_m_fs_kind(ptr, ptr, ptr)
declare i64 @wd_m_env_get(ptr, ptr, ptr)
declare void @wd_m_error_code(ptr, ptr)
declare i64 @wd_m_char_count(ptr, ptr)
declare ptr @wd_m_chars(ptr, ptr)
declare void @wd_m_char_slice(ptr, ptr, ptr, i64, i64)
declare ptr @wd_m_fs_list(ptr, ptr, i64)
declare void @wd_m_byte_str(ptr, ptr, i64)
declare i64 @wd_m_byte_value(ptr, ptr)
declare void @wd_m_str_strip(ptr, ptr)
declare ptr @wd_m_str_split(ptr, ptr, ptr)
declare void @wd_m_str_join(ptr, ptr, ptr, ptr)
declare ptr @wd_m_seq_new(ptr, i64, i64, i64, ptr, ptr)
declare i64 @wd_m_seq_len(ptr)
declare ptr @wd_m_seq_at(ptr, ptr, i64)
declare ptr @wd_m_seq_read(ptr, i64)
declare ptr @wd_m_seq_unique(ptr, ptr, i64)
declare void @wd_m_seq_append(ptr, ptr, ptr)
declare void @wd_m_seq_pop(ptr, ptr, ptr)
declare ptr @wd_m_seq_slice(ptr, ptr, i64, i64, i64, i64, i64)
declare ptr @wd_m_seq_concat(ptr, ptr, ptr)
declare ptr @wd_m_seq_repeat(ptr, ptr, i64)
declare ptr @wd_m_map_new(ptr, i64, i64, i64, ptr, ptr, ptr, ptr)
declare i64 @wd_m_map_len(ptr)
declare i64 @wd_m_map_find(ptr, ptr)
declare ptr @wd_m_map_entry(ptr, i64, i64)
declare ptr @wd_m_map_at(ptr, ptr, ptr)
declare ptr @wd_m_map_edit(ptr, ptr, ptr, i64)
declare ptr @wd_m_map_keys(ptr, ptr, ptr, ptr)
declare i64 @wd_m_utf8_span(ptr, i64)
declare ptr @wd_m_stream_open(ptr, ptr, ptr)
declare void @wd_m_stream_check(ptr, ptr)
declare ptr @wd_m_tcp_connect(ptr,ptr,i64,i64)
declare ptr @wd_m_tcp_listen(ptr,ptr,i64,i64)
declare ptr @wd_m_tcp_accept(ptr,ptr)
declare i64 @wd_m_tcp_port(ptr,ptr)
declare void @wd_m_tcp_shutdown_write(ptr,ptr)
declare void @wd_m_stream_timeout(ptr,ptr,i64)
declare void @wd_m_stream_close(ptr, ptr, i64)
declare i64 @wd_m_stream_closed(ptr)
declare void @wd_m_stream_read(ptr, ptr, ptr, i64)
declare void @wd_m_stream_line(ptr, ptr, ptr, i64)
declare void @wd_m_stream_write(ptr, ptr, ptr)
declare void @wd_m_stream_seek(ptr, ptr, i64)
declare i64 @wd_m_stream_tell(ptr, ptr)
declare void @wd_m_stream_flush(ptr, ptr)
declare void @wd_native_runtime_abi4()
declare ptr @wd_m_array_full(ptr,ptr,i64,ptr,i64)
declare i64 @wd_m_array_len(ptr)
declare i64 @wd_m_array_rank(ptr)
declare i64 @wd_m_array_contiguous(ptr)
declare ptr @wd_m_array_flat(ptr,i64)
declare ptr @wd_m_array_at(ptr,ptr,ptr,i64)
declare ptr @wd_m_array_edit(ptr,ptr,ptr,i64)
declare ptr @wd_m_array_copy(ptr,ptr)
declare ptr @wd_m_array_from_list(ptr,ptr,i64)
declare ptr @wd_m_array_to_list(ptr,ptr)
declare ptr @wd_m_array_shape(ptr,ptr)
declare ptr @wd_m_array_reshape(ptr,ptr,ptr,i64)
declare ptr @wd_m_array_transpose(ptr,ptr,ptr)
declare ptr @wd_m_array_slice(ptr,ptr,i64,i64,i64,i64,i64,i64)
declare ptr @wd_m_array_convert(ptr,ptr,i64)
declare i64 @wd_m_array_equal(ptr,ptr)
declare ptr @wd_m_array_binary(ptr,ptr,i64,ptr,i64,i64)
declare i64 @wd_m_array_reduce_i(ptr,ptr,ptr)
declare double @wd_m_array_reduce_f(ptr,ptr,ptr)
declare ptr @wd_m_array_matmul(ptr,ptr,ptr)
declare ptr @wd_m_array_affine(ptr,ptr,ptr)
declare void @wd_m_array_str(ptr,ptr,ptr)
declare double @llvm.sqrt.f64(double)
declare void @wd_m_format_join(ptr,ptr,ptr,i64)
declare void @wd_m_repr_str(ptr,ptr,ptr)
declare ptr @wd_m_callable_new(ptr, i64, ptr, ptr, ptr, ptr)
declare ptr @wd_m_callable_unique(ptr, ptr)
declare ptr @wd_m_callable_data(ptr)
declare ptr @wd_m_callable_code(ptr)
