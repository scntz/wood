#ifndef WOOD_NATIVE_MANAGED_H
#define WOOD_NATIVE_MANAGED_H

/* Private 64-bit native ABI. No C++ or hosted C library dependency. */
#include <stdint.h>
#include <stddef.h>
typedef int64_t WInt;
typedef uint64_t WSize;
typedef struct WOwner WOwner;
typedef void (*WSlot)(void *);
typedef void (*WInvoke)(void);
struct WOwner { WSize refs, bytes; void (*destroy)(void *); };
typedef struct { unsigned char *data; WInt size; WOwner *owner; } WStr;
typedef struct {
    WOwner owner;
    WInt length, capacity, stride;
    WSlot retain, drop;
} WSeq;
#ifdef __cplusplus
#define WD_NATIVE_ASSERT static_assert
#else
#define WD_NATIVE_ASSERT _Static_assert
#endif
WD_NATIVE_ASSERT(sizeof(void *) == 8, "native runtime requires a qualified 64-bit target");
WD_NATIVE_ASSERT(sizeof(WOwner) == 24 && sizeof(WStr) == 24 && sizeof(WSeq) == 64,
               "native runtime ABI layout mismatch");
#undef WD_NATIVE_ASSERT

/* Implemented by the selected LLVM platform/core module, not libc. */
void *wd_platform_alloc(WSize bytes);
void wd_platform_free(void *memory, WSize bytes);
void wd_rt_error(void *ctx, const char *code, WInt code_size,
                 const char *message, WInt message_size);

void wd_m_retain(void *owner);
void wd_m_release(void *owner);
void wd_native_runtime_abi4(void);
void *wd_m_callable_new(void *ctx, WInt stride, WSlot retain, WSlot drop,
                        WInvoke invoke, const void *value);
void *wd_m_callable_unique(void *ctx, void **slot);
void *wd_m_callable_data(void *value);
WInvoke wd_m_callable_code(void *value);
WSize wd_m_live_count(void);
WSize wd_m_live_bytes(void);
WSize wd_m_peak_bytes(void);
WInt wd_rt_unwinding(void *ctx);
void wd_rt_error_value(void *ctx, const char *code, WInt code_size, const WStr *message);
/* File primitives return negative platform errno values; handles are otherwise
   nonnegative. All byte strings are explicit-length and binary. */
WInt wd_file_open(const void *path, WInt size, WInt mode);
WInt wd_file_read(WInt handle, void *data, WInt size);
WInt wd_file_write(WInt handle, const void *data, WInt size);
WInt wd_file_close(WInt handle);
WInt wd_file_seek(WInt handle, WInt offset, WInt origin);
void *wd_m_stream_open(void *ctx, const WStr *path, const WStr *mode);
void wd_m_stream_check(void *ctx, void *stream);
void wd_m_stream_close(void *ctx, void *stream, WInt report);
WInt wd_m_stream_closed(void *stream);
void wd_m_stream_read(void *ctx, WStr *out, void *stream, WInt count);
void wd_m_stream_line(void *ctx, WStr *out, void *stream, WInt limit);
void wd_m_stream_write(void *ctx, void *stream, const WStr *data);
void wd_m_stream_seek(void *ctx, void *stream, WInt offset);
WInt wd_m_stream_tell(void *ctx, void *stream);
void wd_m_stream_flush(void *ctx, void *stream);
void wd_m_str_retain(void *value);
void wd_m_str_drop(void *value);
void wd_m_str_new(void *ctx, WStr *out, WInt length);
void wd_m_str_concat(void *ctx, WStr *out, const WStr *a, const WStr *b);
void wd_m_str_repeat(void *ctx, WStr *out, const WStr *a, WInt count);
void wd_m_str_slice(void *ctx, WStr *out, const WStr *a, WInt first, WInt last,
                    WInt step, WInt has_first, WInt has_last);
WInt wd_m_find(const WStr *text, const WStr *part);
void wd_m_str_replace(void *ctx, WStr *out, const WStr *a, const WStr *old,
                      const WStr *replacement);
void wd_m_int_str(void *ctx, WStr *out, WInt value);
WInt wd_m_str_int(void *ctx, const WStr *value);
double wd_m_str_float(void *ctx, const WStr *value);
void wd_m_float_str(void *ctx, WStr *out, double value);
void wd_m_byte_str(void *ctx, WStr *out, WInt value);
WInt wd_m_byte_value(void *ctx, const WStr *value);
void wd_m_error_code(void *ctx, const WStr *code);
WInt wd_m_char_count(void *ctx, const WStr *text);
WSeq *wd_m_chars(void *ctx, const WStr *text);
void wd_m_char_slice(void *ctx, WStr *out, const WStr *text, WInt start, WInt stop);
void wd_m_str_strip(WStr *out, const WStr *value);
WSeq *wd_m_seq_new(void *ctx, WInt stride, WInt length, WInt capacity,
                   WSlot retain, WSlot drop);
WInt wd_m_seq_len(const WSeq *value);
void *wd_m_seq_at(void *ctx, const WSeq *value, WInt index);
void *wd_m_seq_read(const WSeq *value, WInt index);
WSeq *wd_m_seq_unique(void *ctx, WSeq **slot, WInt minimum);
void wd_m_seq_append(void *ctx, WSeq **slot, const void *value);
void wd_m_seq_pop(void *ctx, WSeq **slot, void *out);
WSeq *wd_m_seq_slice(void *ctx, const WSeq *value, WInt first, WInt last,
                     WInt step, WInt has_first, WInt has_last);
WSeq *wd_m_seq_concat(void *ctx, const WSeq *a, const WSeq *b);
WSeq *wd_m_seq_repeat(void *ctx, const WSeq *a, WInt count);
WSeq *wd_m_str_split(void *ctx, const WStr *text, const WStr *delimiter);
void wd_m_str_join(void *ctx, WStr *out, const WSeq *values, const WStr *separator);

typedef int32_t (*WCompare)(const void *, const void *);
typedef struct WMap WMap;
/* Additive ABI-4 opaque arrays; existing owner/callable/stream layouts unchanged. */
void *wd_m_array_full(void *,const WInt *,WInt,const void *,WInt);
WInt wd_m_array_len(const void *);
WInt wd_m_array_rank(const void *);
WInt wd_m_array_contiguous(const void *);
void *wd_m_array_flat(const void *,WInt);
void *wd_m_array_at(void *,const void *,const WInt *,WInt);
void *wd_m_array_edit(void *,void **,const WInt *,WInt);
void *wd_m_array_borrow(void *,void **,WInt);
double wd_m_math_sin(void *,double);
double wd_m_math_cos(void *,double);
void *wd_m_array_copy(void *,const void *);
void *wd_m_array_from_list(void *,const WSeq *,WInt);
void *wd_m_array_to_list(void *,const void *);
void *wd_m_array_shape(void *,const void *);
void *wd_m_array_reshape(void *,const void *,const WInt *,WInt);
void *wd_m_array_transpose(void *,const void *,const WSeq *);
void *wd_m_array_slice(void *,const void *,WInt,WInt,WInt,WInt,WInt,WInt);
void *wd_m_array_convert(void *,const void *,WInt);
WInt wd_m_array_equal(const void *,const void *);
void *wd_m_array_binary(void *,const void *,WInt,const void *,WInt,WInt);
WInt wd_m_array_reduce_i(void *,const void *,const void *);
double wd_m_array_reduce_f(void *,const void *,const void *);
void *wd_m_array_matmul(void *,const void *,const void *);
void *wd_m_array_affine(void *,const void *,const void *);
void wd_m_array_str(void *,WStr *,const void *);
void wd_m_format_join(void *,WStr *,const WStr *,WInt);
void wd_m_repr_str(void *,WStr *,const WStr *);
void wd_m_format_value(void *,WStr *,const void *,const void *,WInt);
WMap *wd_m_map_new(void *ctx, WInt stride, WInt value_offset, WInt key_size,
                   WSlot retain_entry, WSlot drop_entry, WSlot retain_key,
                   WCompare compare);
WInt wd_m_map_len(const WMap *map);
WInt wd_m_map_find(const WMap *map, const void *key);
void *wd_m_map_entry(const WMap *map, WInt index, WInt value);
void *wd_m_map_at(void *ctx, const WMap *map, const void *key);
void *wd_m_map_edit(void *ctx, WMap **slot, const void *key, WInt insert);
WInt wd_m_map_remove(void *,WMap **,const void *);
WInt wd_m_eof(void *);
void wd_m_read_line(void *,WStr *);
WInt wd_m_time_ns(void *);
double wd_m_time_seconds(void *);
void wd_m_time_sleep(void *,double);
/* Internal exact scaling helper: 0 <= remainder < frequency <= INT64_MAX. */
WInt wd_m_time_fraction(WInt remainder,WInt frequency);
void wd_m_threads_init(void);
WInt wd_m_worker_thread(void);
void *wd_m_task_spawn(void *,void *,void *);
void *wd_m_task_wait(void *,void *,WInt);
void wd_m_task_check(void *,void *);
WInt wd_m_task_done(void *,void *);
void wd_m_task_cancel(void *,void *);
void wd_m_task_close(void *);
WInt wd_m_task_closed(void *);
WInt wd_m_task_cancelled(void);
void wd_m_task_checkpoint(void *);
WInt wd_m_db_closed(void *,void *);
WInt wd_m_stmt_closed(void *);
WInt wd_m_transaction_closed(void *);
WInt wd_m_tls_context_closed(void *);
void wd_m_callback_push(void *,const void *,void *,void *);
void *wd_m_callback_find(const void *);
void wd_m_callback_pop(void *);
WSeq *wd_m_map_keys(void *ctx, const WMap *map, WSlot retain, WSlot drop);

typedef struct WJson WJson;
void *wd_m_json_parse(void *ctx, const WStr *text);
void wd_m_json_dump(void *ctx, WStr *out, const WJson *value);
void *wd_m_json_dict(void *ctx, const WJson *value);
void *wd_m_json_list(void *ctx, const WJson *value);
void wd_m_json_str(void *ctx, WStr *out, const WJson *value);
WInt wd_m_json_equal(const WJson *a, const WJson *b);
void wd_m_args_init(WInt argc, const void *argv);
void *wd_m_args(void *ctx);
void wd_m_read_text(void *,WStr *,const WStr *);
void wd_m_write_text(void *,const WStr *,const WStr *);
void wd_m_path_join(void *ctx, WStr *out, const WStr *a, const WStr *b);
void wd_m_path_name(void *ctx, WStr *out, const WStr *path);
void wd_m_path_parent(void *ctx, WStr *out, const WStr *path);
void wd_m_path_normalize(void *ctx, WStr *out, const WStr *path);
void wd_m_path_absolute(void *ctx, WStr *out, const WStr *path);
void wd_m_path_resolve(void *ctx, WStr *out, const WStr *path, WInt allow_missing);
void wd_m_path_identity(void *ctx, WStr *out, const WStr *path);
WInt wd_m_path_exists(void *ctx, const WStr *path);
WInt wd_m_path_is_absolute(void *ctx, const WStr *path);
WInt wd_m_path_within(void *ctx, const WStr *path, const WStr *root);
void wd_m_fs_kind(void *ctx, WStr *out, const WStr *path);
void *wd_m_fs_list(void *ctx, const WStr *path, WInt limit);
WInt wd_m_fs_size(void *,const WStr *);
WInt wd_m_fs_mkdirs(void *,const WStr *);
WInt wd_m_fs_remove(void *,const WStr *);
#ifdef _WIN32
WInt wd_rt_context_size(void);
void wd_rt_context_error(void *,WStr *,WStr *);
void wd_rt_context_clear(void *);
void wd_rt_context_forward(void *,void *);
void wd_rt_context_site(void *,const WStr *,WInt,WInt);
#else
/* Older ABI-4 application objects have no private driver context API. */
WInt wd_rt_context_size(void) __attribute__((weak));
void wd_rt_context_error(void *,WStr *,WStr *) __attribute__((weak));
void wd_rt_context_clear(void *) __attribute__((weak));
void wd_rt_context_forward(void *,void *) __attribute__((weak));
void wd_rt_context_site(void *,const WStr *,WInt,WInt) __attribute__((weak));
#endif

#endif
