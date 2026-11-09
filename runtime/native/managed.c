/* Freestanding C17 primitives for the Wood LLVM ABI. This file is compiled as C,
   never C++, and uses no hosted library. Allocation is supplied by the OS module.
   All public functions take scalars/pointers, avoiding aggregate C ABI variance. */
#include "managed.h"
#include "wasm.inc"
#include "sync.inc"

static WSize live_count, live_bytes, peak_bytes;
static const WInt maximum = INT64_MAX;
/* Set before the first worker thread exists. Until then nothing can race, so reference counts
   and statistics skip the cost of atomic instructions. Threads inside loaded libraries such as
   CPython or OpenSSL never touch these owners. */
static int shared_owners;
/* Set before the first task starts and never cleared, so a relaxed read is enough. */
static inline int owners_shared(void) { return __atomic_load_n(&shared_owners,__ATOMIC_RELAXED); }

/* Asking the OS for every small block is by far the most expensive part of running a program
   that allocates: one mmap, one page fault and one munmap each on Linux. Small blocks are carved
   from larger chunks and recycled by size. Chunks stay with the process, so peak memory is the
   high-water mark of what the program itself used. */
enum { HEAP_SMALL = 512, HEAP_LIMIT = 65536, HEAP_CHUNK = 1048576, HEAP_CLASSES = 40 };
static WLock heap_lock;
static void *heap_free[HEAP_CLASSES];
static unsigned char *heap_next;
static WSize heap_left;
static unsigned heap_class(WSize bytes) {
    if (bytes <= HEAP_SMALL) return (unsigned)((bytes + 15) / 16);
    unsigned index = 33;
    for (WSize size = 1024; size < bytes; size <<= 1) ++index;
    return index;
}
static WSize heap_class_size(unsigned index) {
    return index <= 32 ? (WSize)index * 16 : (WSize)1024 << (index - 33);
}
/* AddressSanitizer can only see each block if each block has its own allocation, and
   a test that fails allocations needs them to reach the platform. */
#if defined(__SANITIZE_ADDRESS__) && !defined(WD_HEAP_BYPASS)
#define WD_HEAP_BYPASS 1
#elif defined(__has_feature) && !defined(WD_HEAP_BYPASS)
#if __has_feature(address_sanitizer)
#define WD_HEAP_BYPASS 1
#endif
#endif
#ifndef WD_HEAP_BYPASS
#define WD_HEAP_BYPASS 0
#endif
static void *heap_alloc(WSize bytes) {
    if (bytes > HEAP_LIMIT || WD_HEAP_BYPASS) return wd_platform_alloc(bytes);
    unsigned index = heap_class(bytes);
    void *block;
    if (owners_shared()) w_lock(&heap_lock);
    block = heap_free[index];
    if (block) {
        heap_free[index] = *(void **)block;
    } else {
        WSize size = heap_class_size(index);
        if (heap_left < size) {
            /* What is left of the old chunk is too small to matter. */
            heap_next = wd_platform_alloc(HEAP_CHUNK);
            heap_left = heap_next ? HEAP_CHUNK : 0;
        }
        if (heap_left >= size) {
            block = heap_next;
            heap_next += size;
            heap_left -= size;
        }
    }
    if (owners_shared()) w_unlock(&heap_lock);
    return block;
}
static void heap_release(void *block, WSize bytes) {
    if (bytes > HEAP_LIMIT || WD_HEAP_BYPASS) {
        wd_platform_free(block, bytes);
        return;
    }
    unsigned index = heap_class(bytes);
    if (owners_shared()) w_lock(&heap_lock);
    *(void **)block = heap_free[index];
    heap_free[index] = block;
    if (owners_shared()) w_unlock(&heap_lock);
}

static void fail(void *ctx, const char *code, const char *message) {
    WInt size = 0;
    while (message[size]) ++size;
    wd_rt_error(ctx, code, 5, message, size);
}
static void copy_bytes(void *dst, const void *src, WSize count) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (WSize i = 0; i < count; ++i) d[i] = s[i];
}
static void zero_bytes(void *dst, WSize count) {
    unsigned char *d = dst;
    for (WSize i = 0; i < count; ++i) d[i] = 0;
}
static int sum(void *ctx, WInt a, WInt b, WInt *out) {
    if (a < 0 || b < 0 || a > maximum - b) {
        fail(ctx, "R4003", "sequence is too large");
        return 0;
    }
    *out = a + b;
    return 1;
}
static int product(void *ctx, WInt a, WInt b, WInt *out) {
    if (a < 0 || b < 0 || (a && b > maximum / a)) {
        fail(ctx, "R4003", "sequence is too large");
        return 0;
    }
    *out = a * b;
    return 1;
}
static WOwner *allocate(void *ctx, WInt bytes, void (*destroy)(void *)) {
    void *p = heap_alloc((WSize)bytes);
    if (!p) {
        fail(ctx, "R4099", "memory allocation failed");
        return 0;
    }
    WOwner *owner = p;
    owner->refs = 1;
    owner->bytes = (WSize)bytes;
    owner->destroy = destroy;
    if (!owners_shared()) {
        ++live_count;
        live_bytes += (WSize)bytes;
        if (live_bytes > peak_bytes) peak_bytes = live_bytes;
        return owner;
    }
    __atomic_fetch_add(&live_count,1,__ATOMIC_RELAXED);
    WSize current=__atomic_add_fetch(&live_bytes,(WSize)bytes,__ATOMIC_RELAXED);
    WSize peak=__atomic_load_n(&peak_bytes,__ATOMIC_RELAXED);
    while(current>peak && !__atomic_compare_exchange_n(&peak_bytes,&peak,current,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
    return owner;
}
void wd_m_retain(void *value) {
    WOwner *owner = value;
    if (owner) {
        WSize old=__atomic_load_n(&owner->refs,__ATOMIC_RELAXED);
        if (!owners_shared()) {
            if (old == UINT64_MAX) return;
            if (!old || old == UINT64_MAX-1) __builtin_trap();
            owner->refs = old+1;
            return;
        }
        do {
            if(old==UINT64_MAX) return; /* Immutable typed empty value. */
            if(!old || old==UINT64_MAX-1) __builtin_trap();
        } while(!__atomic_compare_exchange_n(&owner->refs,&old,old+1,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    }
}
void wd_m_release(void *value) {
    WOwner *owner = value;
    if (owner) {
        if(__atomic_load_n(&owner->refs,__ATOMIC_RELAXED)==UINT64_MAX) return;
        WSize old;
        if (owners_shared()) {
            old=__atomic_fetch_sub(&owner->refs,1,__ATOMIC_ACQ_REL);
        } else {
            old=owner->refs;
            owner->refs=old-1;
        }
        if(!old) __builtin_trap();
        if(old==1) {
            WSize bytes = owner->bytes;
            owner->destroy(owner);
            if (owners_shared()) {
                __atomic_fetch_sub(&live_count,1,__ATOMIC_RELAXED);
                __atomic_fetch_sub(&live_bytes,bytes,__ATOMIC_RELAXED);
            } else {
                --live_count;
                live_bytes -= bytes;
            }
            heap_release(owner, bytes);
        }
    }
}
WSize wd_m_live_count(void) { return __atomic_load_n(&live_count,__ATOMIC_RELAXED); }
WSize wd_m_live_bytes(void) { return __atomic_load_n(&live_bytes,__ATOMIC_RELAXED); }
WSize wd_m_peak_bytes(void) { return __atomic_load_n(&peak_bytes,__ATOMIC_RELAXED); }
static void no_children(void *value) { (void)value; }
void wd_m_str_retain(void *value) { wd_m_retain(((WStr *)value)->owner); }
void wd_m_str_drop(void *value) { wd_m_release(((WStr *)value)->owner); }
static void str_copy(WStr *out, const WStr *value) {
    wd_m_retain(value->owner);
    *out = *value;
}
void wd_m_str_new(void *ctx, WStr *out, WInt length) {
    *out = (WStr){0};
    WInt bytes;
    if (!sum(ctx, length, (WInt)sizeof(WOwner) + 1, &bytes)) return;
    WOwner *owner = allocate(ctx, bytes, no_children);
    if (!owner) return;
    out->data = (unsigned char *)(owner + 1);
    out->data[length] = 0;
    out->size = length;
    out->owner = owner;
}
void wd_m_str_concat(void *ctx, WStr *out, const WStr *a, const WStr *b) {
    *out = (WStr){0};
    if (!a->size) { str_copy(out, b); return; }
    if (!b->size) { str_copy(out, a); return; }
    WInt size;
    if (!sum(ctx, a->size, b->size, &size)) return;
    wd_m_str_new(ctx, out, size);
    if (!out->owner) return;
    copy_bytes(out->data, a->data, (WSize)a->size);
    copy_bytes(out->data + a->size, b->data, (WSize)b->size);
}
void wd_m_str_repeat(void *ctx, WStr *out, const WStr *a, WInt count) {
    *out = (WStr){0};
    if (count <= 0 || !a->size) return;
    if (count == 1) { str_copy(out, a); return; }
    WInt size;
    if (!product(ctx, a->size, count, &size)) return;
    wd_m_str_new(ctx, out, size);
    if (!out->owner) return;
    for (WInt i = 0; i < size; i += a->size)
        copy_bytes(out->data + i, a->data, (WSize)a->size);
}
typedef struct { WInt first, step, count; } Slice;
static WInt bound(WInt x, WInt size, int reverse) {
    if (x < 0) return x < -size ? (reverse ? -1 : 0) : x + size;
    return x >= size ? (reverse ? size - 1 : size) : x;
}
static Slice slice(void *ctx, WInt size, WInt first, WInt last, WInt step,
                    WInt has_first, WInt has_last) {
    if (!step) {
        fail(ctx, "R4005", "slice step cannot be zero");
        return (Slice){0, 1, 0};
    }
    int reverse = step < 0;
    first = has_first ? bound(first, size, reverse) : (reverse ? size - 1 : 0);
    last = has_last ? bound(last, size, reverse) : (reverse ? -1 : size);
    WInt count = 0;
    if (!reverse && first < last) count = 1 + (last - 1 - first) / step;
    if (reverse && first > last)
        count = (WInt)(1 + (WSize)(first - 1 - last) / (0 - (WSize)step));
    return (Slice){first, step, count};
}
void wd_m_str_slice(void *ctx, WStr *out, const WStr *a, WInt first, WInt last,
                    WInt step, WInt has_first, WInt has_last) {
    *out = (WStr){0};
    Slice s = slice(ctx, a->size, first, last, step, has_first, has_last);
    if (!step || !s.count) return;
    if (step == 1) {
        wd_m_retain(a->owner);
        *out = (WStr){a->data + s.first, s.count, a->owner};
        return;
    }
    wd_m_str_new(ctx, out, s.count);
    if (!out->owner) return;
    WInt p = s.first;
    for (WInt i = 0; i < s.count; ++i) {
        out->data[i] = a->data[p];
        if (i + 1 < s.count) p += step;
    }
}
static int equal_bytes(const unsigned char *a, const unsigned char *b, WInt size) {
    for (WInt i = 0; i < size; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
WInt wd_m_find(const WStr *text, const WStr *part) {
    if (!part->size) return 0;
    if (part->size > text->size) return -1;
    for (WInt i = 0; i <= text->size - part->size; ++i)
        if (equal_bytes(text->data + i, part->data, part->size)) return i;
    return -1;
}
void wd_m_str_replace(void *ctx, WStr *out, const WStr *a, const WStr *old,
                      const WStr *replacement) {
    *out = (WStr){0};
    WInt matches = 0, offset = 0, n;
    if (!old->size) {
        fail(ctx,"R4009","replacement pattern cannot be empty"); return;
    } else {
        while (offset <= a->size - old->size) {
            WStr tail = {a->data ? a->data + offset : 0, a->size - offset, a->owner};
            n = wd_m_find(&tail, old);
            if (n < 0) break;
            ++matches;
            offset += n + old->size;
        }
    }
    if (!matches) { str_copy(out, a); return; }
    WInt inserted, removed, size;
    if (!product(ctx, matches, replacement->size, &inserted) ||
        !product(ctx, matches, old->size, &removed) ||
        !sum(ctx, a->size - removed, inserted, &size)) return;
    wd_m_str_new(ctx, out, size);
    if (!out->owner) return;
    WInt written = 0;
    offset = 0;
    for (WInt i = 0; i < matches; ++i) {
        WStr tail = {a->data ? a->data + offset : 0, a->size - offset, a->owner};
        n = wd_m_find(&tail, old);
        copy_bytes(out->data + written, tail.data, (WSize)n);
        written += n;
        copy_bytes(out->data + written, replacement->data, (WSize)replacement->size);
        written += replacement->size;
        offset += n + old->size;
    }
    copy_bytes(out->data + written, a->data ? a->data + offset : 0, (WSize)(a->size - offset));
}
void wd_m_int_str(void *ctx, WStr *out, WInt value) {
    unsigned char text[21];
    WInt begin = 21;
    WSize n = value < 0 ? 0 - (WSize)value : (WSize)value;
    do { text[--begin] = (unsigned char)('0' + n % 10); n /= 10; } while (n);
    if (value < 0) text[--begin] = '-';
    wd_m_str_new(ctx, out, 21 - begin);
    if (out->owner) copy_bytes(out->data, text + begin, (WSize)(21 - begin));
}
WInt wd_m_str_int(void *ctx, const WStr *value) {
    WInt i = 0;
    if (value->size && value->data[0] == '+') {
        ++i;
        if (i < value->size && value->data[i] == '-') {
            fail(ctx, "R4003", "invalid decimal integer sign");
            return 0;
        }
    }
    int negative = value->size && value->data[0] == '-';
    if (negative) ++i;
    WSize result = 0, limit = (WSize)maximum + (negative ? 1u : 0u);
    if (i == value->size) goto invalid;
    for (; i < value->size; ++i) {
        unsigned char c = value->data[i];
        if (c < '0' || c > '9' || result > (limit - (WSize)(c - '0')) / 10) goto invalid;
        result = result * 10 + (WSize)(c - '0');
    }
    if (negative && result == (WSize)maximum + 1) return INT64_MIN;
    return negative ? -(WInt)result : (WInt)result;
invalid:
    fail(ctx, "R4003", "invalid decimal integer or value outside signed 64-bit range");
    return 0;
}
void wd_m_byte_str(void *ctx, WStr *out, WInt value) {
    *out = (WStr){0};
    if (value < 0 || value > 255) {
        fail(ctx, "R4013", "byte_string requires an integer in 0..255"); return;
    }
    wd_m_str_new(ctx, out, 1);
    if (out->owner) out->data[0] = (unsigned char)value;
}
WInt wd_m_byte_value(void *ctx, const WStr *value) {
    if (value->size != 1) {
        fail(ctx, "R4013", "byte_value requires exactly one byte"); return 0;
    }
    return value->data[0];
}
/* error(code, message): a program's own code is 1 to 64 ASCII letters, digits, '.', '_' or '-' and starts
   with a letter. A capital letter and four digits is the shape of wood's own codes, so it is refused. */
void wd_m_error_code(void *ctx, const WStr *code) {
    int valid = code->size >= 1 && code->size <= 64, reserved = code->size == 5;
    for (WInt i = 0; valid && i < code->size; ++i) {
        unsigned char c = code->data[i];
        int letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'), digit = c >= '0' && c <= '9';
        valid = letter || (i > 0 && (digit || c == '.' || c == '_' || c == '-'));
        reserved = reserved && (i ? digit : c >= 'A' && c <= 'Z');
    }
    if (!valid)
        fail(ctx, "R4003", "error code must be 1 to 64 letters, digits, '.', '_' or '-', starting with a letter");
    else if (reserved)
        fail(ctx, "R4003", "codes like R4010 are wood's own; use a name of your own, such as app.not_found");
}
void wd_m_str_strip(WStr *out, const WStr *value) {
    WInt first = 0, last = value->size;
    while (first < last && (value->data[first] == ' ' ||
           (value->data[first] >= 9 && value->data[first] <= 13))) ++first;
    while (last > first && (value->data[last - 1] == ' ' ||
           (value->data[last - 1] >= 9 && value->data[last - 1] <= 13))) --last;
    wd_m_retain(value->owner);
    *out = (WStr){value->data ? value->data + first : 0, last - first, value->owner};
}

static unsigned char *data(const WSeq *seq) { return (unsigned char *)(seq + 1); }
static void seq_destroy(void *value) {
    WSeq *seq = value;
    if (seq->drop)
        for (WInt i = 0; i < seq->length; ++i) seq->drop(data(seq) + i * seq->stride);
}
WSeq *wd_m_seq_new(void *ctx, WInt stride, WInt length, WInt capacity,
                   WSlot retain, WSlot drop) {
    WInt payload, bytes;
    if (stride <= 0 || length < 0 || capacity < length) __builtin_trap();
    if (!product(ctx, stride, capacity, &payload) ||
        !sum(ctx, payload, (WInt)sizeof(WSeq), &bytes)) return 0;
    WSeq *seq = (WSeq *)allocate(ctx, bytes, seq_destroy);
    if (!seq) return 0;
    seq->length = length; seq->capacity = capacity; seq->stride = stride;
    seq->retain = retain; seq->drop = drop;
    zero_bytes(data(seq), (WSize)payload);
    return seq;
}
WInt wd_m_seq_len(const WSeq *value) { return value ? value->length : 0; }
void *wd_m_seq_read(const WSeq *value, WInt index) {
    return data(value) + index * value->stride;
}
void *wd_m_seq_at(void *ctx, const WSeq *value, WInt index) {
    WInt size = wd_m_seq_len(value);
    if (index < 0) index += size;
    if (index < 0 || index >= size) {
        fail(ctx, "R4006", "index out of bounds"); return 0;
    }
    return data(value) + index * value->stride;
}
WSeq *wd_m_seq_unique(void *ctx, WSeq **slot, WInt minimum) {
    WSeq *old = *slot;
    if (!old || minimum < 0) __builtin_trap();
    int exclusive = owner_refs(&old->owner) == 1;
    if (exclusive && old->capacity >= minimum) return old;
    WInt capacity = old->capacity;
    if (capacity < minimum) {
        WInt extra = capacity / 2 + 1;
        capacity = capacity <= maximum - extra ? capacity + extra : maximum;
        if (capacity < minimum) capacity = minimum;
    }
    WSeq *next = wd_m_seq_new(ctx, old->stride, old->length, capacity, old->retain, old->drop);
    if (!next) return 0;
    copy_bytes(data(next), data(old), (WSize)(old->length * old->stride));
    /* A unique buffer can transfer its owned prefix bit-for-bit. Publish only
       after allocation/copy succeed, and disarm the old destructor's elements.
       Shared snapshots still need independent ownership for every child. */
    if (exclusive) old->length = 0;
    else if (next->retain)
        for (WInt i = 0; i < next->length; ++i) next->retain(data(next) + i * next->stride);
    *slot = next;
    wd_m_release(old);
    return next;
}
void wd_m_seq_append(void *ctx, WSeq **slot, const void *value) {
    WSeq *old = *slot;
    if (old->length == maximum) { fail(ctx, "R4003", "list is too large"); return; }
    WSeq *seq = wd_m_seq_unique(ctx, slot, old->length + 1);
    if (!seq) return;
    unsigned char *item = data(seq) + seq->length * seq->stride;
    copy_bytes(item, value, (WSize)seq->stride);
    if (seq->retain) seq->retain(item);
    ++seq->length;
}
void wd_m_seq_pop(void *ctx, WSeq **slot, void *out) {
    WSeq *seq = *slot;
    if (!seq->length) { fail(ctx, "R4007", "cannot pop an empty list"); return; }
    seq = wd_m_seq_unique(ctx, slot, seq->capacity);
    if (!seq) return;
    --seq->length;
    /* Transfer ownership out; the now-inactive slot owns nothing. */
    copy_bytes(out, data(seq) + seq->length * seq->stride, (WSize)seq->stride);
    zero_bytes(data(seq) + seq->length * seq->stride, (WSize)seq->stride);
}
WSeq *wd_m_seq_slice(void *ctx, const WSeq *value, WInt first, WInt last,
                     WInt step, WInt has_first, WInt has_last) {
    Slice s = slice(ctx, value->length, first, last, step, has_first, has_last);
    if (!step) return 0;
    if (s.first == 0 && s.count == value->length && s.step == 1) {
        wd_m_retain((void *)value); return (WSeq *)value;
    }
    WSeq *seq = wd_m_seq_new(ctx, value->stride, s.count, s.count, value->retain, value->drop);
    if (!seq) return 0;
    WInt p = s.first;
    for (WInt i = 0; i < s.count; ++i) {
        void *item = data(seq) + i * seq->stride;
        copy_bytes(item, data(value) + p * value->stride, (WSize)seq->stride);
        if (seq->retain) seq->retain(item);
        if (i + 1 < s.count) p += step;
    }
    return seq;
}
WSeq *wd_m_seq_concat(void *ctx, const WSeq *a, const WSeq *b) {
    WInt size;
    if (!sum(ctx, a->length, b->length, &size)) return 0;
    WSeq *seq = wd_m_seq_new(ctx, a->stride, size, size, a->retain, a->drop);
    if (!seq) return 0;
    copy_bytes(data(seq), data(a), (WSize)(a->length * a->stride));
    copy_bytes(data(seq) + a->length * a->stride, data(b), (WSize)(b->length * b->stride));
    if (seq->retain)
        for (WInt i = 0; i < size; ++i) seq->retain(data(seq) + i * seq->stride);
    return seq;
}
WSeq *wd_m_seq_repeat(void *ctx, const WSeq *a, WInt count) {
    WInt size = 0;
    if (count > 0 && !product(ctx, a->length, count, &size)) return 0;
    WSeq *seq = wd_m_seq_new(ctx, a->stride, size, size, a->retain, a->drop);
    if (!seq) return 0;
    for (WInt i = 0; i < size; ++i) {
        void *item = data(seq) + i * seq->stride;
        copy_bytes(item, data(a) + (i % a->length) * a->stride, (WSize)a->stride);
        if (seq->retain) seq->retain(item);
    }
    return seq;
}
WSeq *wd_m_str_split(void *ctx, const WStr *text, const WStr *delimiter) {
    if (!delimiter->size) { fail(ctx, "R4009", "split separator cannot be empty"); return 0; }
    WSeq *seq = wd_m_seq_new(ctx, sizeof(WStr), 0, 0, wd_m_str_retain, wd_m_str_drop);
    if (!seq) return 0;
    WInt offset = 0;
    for (;;) {
        WStr tail = {text->data ? text->data + offset : 0, text->size - offset, text->owner};
        WInt n = wd_m_find(&tail, delimiter);
        WStr item = {tail.data, n < 0 ? tail.size : n, text->owner};
        WInt before = seq->length;
        wd_m_seq_append(ctx, &seq, &item);
        if (seq->length == before) { wd_m_release(seq); return 0; }
        if (n < 0) break;
        offset += n + delimiter->size;
    }
    return seq;
}
/* Characters are strict UTF-8 scalar values: no overlong forms, surrogates or values past U+10FFFF.
   Returns the byte width of the character at offset, or 0 when the bytes there are not valid UTF-8. */
static WInt char_width(const WStr *text, WInt at) {
    unsigned char first = text->data[at];
    WInt width = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2
               : first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    if (!width || width > text->size - at) return 0;
    for (WInt i = 1; i < width; ++i)
        if ((text->data[at + i] & 0xC0) != 0x80) return 0;
    if (width > 1) {
        unsigned char second = text->data[at + 1];
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second > 0x9F) ||
            (first == 0xF0 && second < 0x90) || (first == 0xF4 && second > 0x8F)) return 0;
    }
    return width;
}
/* The number of characters, or -1 after reporting invalid UTF-8. */
static WInt char_total(void *ctx, const WStr *text) {
    WInt count = 0;
    for (WInt at = 0; at < text->size; ++count) {
        WInt width = char_width(text, at);
        if (!width) { fail(ctx, "R4013", "text is not valid UTF-8"); return -1; }
        at += width;
    }
    return count;
}
WInt wd_m_char_count(void *ctx, const WStr *text) {
    WInt count = char_total(ctx, text);
    return count < 0 ? 0 : count;
}
/* Each character is a view of the text's own bytes. */
WSeq *wd_m_chars(void *ctx, const WStr *text) {
    WInt count = char_total(ctx, text);
    if (count < 0) return 0;
    WSeq *seq = wd_m_seq_new(ctx, sizeof(WStr), 0, count, wd_m_str_retain, wd_m_str_drop);
    if (!seq) return 0;
    for (WInt at = 0; at < text->size;) {
        WInt width = char_width(text, at);
        WStr item = {text->data + at, width, text->owner};
        WInt before = seq->length;
        wd_m_seq_append(ctx, &seq, &item);
        if (seq->length == before) { wd_m_release(seq); return 0; }
        at += width;
    }
    return seq;
}
/* Characters start..stop with the bounds of slice: negative counts from the end, then clamped. */
void wd_m_char_slice(void *ctx, WStr *out, const WStr *text, WInt start, WInt stop) {
    *out = (WStr){0};
    WInt count = char_total(ctx, text);
    if (count < 0) return;
    if (start < 0) start = start < -count ? 0 : start + count;
    else if (start > count) start = count;
    if (stop < 0) stop = stop < -count ? 0 : stop + count;
    else if (stop > count) stop = count;
    if (stop < start) stop = start;
    WInt first = 0, at = 0;
    for (WInt index = 0; index < stop; ++index) {
        if (index == start) first = at;
        at += char_width(text, at);
    }
    if (start == stop) first = at;
    wd_m_retain(text->owner);
    *out = (WStr){text->data ? text->data + first : 0, at - first, text->owner};
}
void wd_m_str_join(void *ctx, WStr *out, const WSeq *values, const WStr *separator) {
    *out = (WStr){0};
    WInt size = 0, separators;
    if (!product(ctx, values->length ? values->length - 1 : 0, separator->size, &separators)) return;
    size = separators;
    for (WInt i = 0; i < values->length; ++i) {
        const WStr *item = (const WStr *)(data(values) + i * values->stride);
        if (!sum(ctx, size, item->size, &size)) return;
    }
    wd_m_str_new(ctx, out, size);
    if (!out->owner) return;
    WInt p = 0;
    for (WInt i = 0; i < values->length; ++i) {
        const WStr *item = (const WStr *)(data(values) + i * values->stride);
        if (i) { copy_bytes(out->data + p, separator->data, (WSize)separator->size); p += separator->size; }
        copy_bytes(out->data + p, item->data, (WSize)item->size);
        p += item->size;
    }
}

/* Insertion-ordered entries plus an AVL index. No predictable string hashing.
   Index links refer to stable integer slots, not reallocatable element pointers. */
typedef struct { WInt left, right, height, entry; } WNode;
struct WMap {
    WOwner owner;
    WSeq *entries, *nodes;
    WInt root, value_offset, key_size;
    WSlot retain_key;
    WCompare compare;
};
static WNode *node(WMap *m, WInt i) { return (WNode *)(data(m->nodes) + i * sizeof(WNode)); }
static const void *key_at(const WMap *m, WInt i) {
    return data(m->entries) + i * m->entries->stride;
}
static void map_destroy(void *value) {
    WMap *m = value;
    wd_m_release(m->entries); wd_m_release(m->nodes);
}
WMap *wd_m_map_new(void *ctx, WInt stride, WInt value_offset, WInt key_size,
                   WSlot retain_entry, WSlot drop_entry, WSlot retain_key,
                   WCompare compare) {
    if (!compare || key_size <= 0 || value_offset < key_size || value_offset > stride)
        __builtin_trap();
    WMap *m = (WMap *)allocate(ctx, sizeof(WMap), map_destroy);
    if (!m) return 0;
    m->entries = 0; m->nodes = 0; m->root = -1;
    m->value_offset = value_offset; m->key_size = key_size;
    m->retain_key = retain_key; m->compare = compare;
    m->entries = wd_m_seq_new(ctx, stride, 0, 0, retain_entry, drop_entry);
    if (!m->entries) { wd_m_release(m); return 0; }
    m->nodes = wd_m_seq_new(ctx, sizeof(WNode), 0, 0, 0, 0);
    if (!m->nodes) { wd_m_release(m); return 0; }
    return m;
}
WInt wd_m_map_len(const WMap *m) { return m ? m->entries->length : 0; }
WInt wd_m_map_find(const WMap *m, const void *key) {
    WInt current = m ? m->root : -1;
    while (current >= 0) {
        const WNode *n = (const WNode *)(data(m->nodes) + current * sizeof(WNode));
        int32_t order = m->compare(key, key_at(m, n->entry));
        if (!order) return n->entry;
        current = order < 0 ? n->left : n->right;
    }
    return -1;
}
void *wd_m_map_entry(const WMap *m, WInt index, WInt value) {
    return data(m->entries) + index * m->entries->stride + (value ? m->value_offset : 0);
}
void *wd_m_map_at(void *ctx, const WMap *m, const void *key) {
    WInt index = wd_m_map_find(m, key);
    if (index < 0) { fail(ctx, "R4011", "dictionary key is absent"); return 0; }
    return wd_m_map_entry(m, index, 1);
}
WSeq *wd_m_map_keys(void *ctx, const WMap *map, WSlot retain, WSlot drop) {
    WInt length = wd_m_map_len(map);
    WSeq *keys = wd_m_seq_new(ctx,map->key_size,length,length,retain,drop);
    if (!keys) return 0;
    for (WInt i = 0; i < length; ++i) {
        void *target = data(keys) + i * keys->stride;
        copy_bytes(target,wd_m_map_entry(map,i,0),(WSize)map->key_size);
        if (retain) retain(target);
    }
    return keys;
}
static WMap *map_unique(void *ctx, WMap **slot) {
    WMap *old = *slot;
    if (owner_refs(&old->owner) == 1) return old;
    WMap *m = (WMap *)allocate(ctx, sizeof(WMap), map_destroy);
    if (!m) return 0;
    m->entries = old->entries; m->nodes = old->nodes; m->root = old->root;
    m->value_offset = old->value_offset; m->key_size = old->key_size;
    m->retain_key = old->retain_key; m->compare = old->compare;
    wd_m_retain(m->entries); wd_m_retain(m->nodes);
    *slot = m;
    wd_m_release(old);
    return m;
}
static WInt height(WMap *m, WInt i) { return i < 0 ? 0 : node(m, i)->height; }
static void refresh(WMap *m, WInt i) {
    WNode *n = node(m, i);
    WInt a = height(m, n->left), b = height(m, n->right);
    n->height = 1 + (a > b ? a : b);
}
static WInt left(WMap *m, WInt root) {
    WInt next = node(m, root)->right, middle = node(m, next)->left;
    node(m, root)->right = middle; node(m, next)->left = root;
    refresh(m, root); refresh(m, next);
    return next;
}
static WInt right(WMap *m, WInt root) {
    WInt next = node(m, root)->left, middle = node(m, next)->right;
    node(m, root)->left = middle; node(m, next)->right = root;
    refresh(m, root); refresh(m, next);
    return next;
}
static WInt avl_insert(WMap *m, WInt root, WInt fresh) {
    if (root < 0) return fresh;
    WNode *n = node(m, root);
    const void *key = key_at(m, node(m, fresh)->entry);
    int32_t order = m->compare(key, key_at(m, n->entry));
    if (order < 0) n->left = avl_insert(m, n->left, fresh);
    else n->right = avl_insert(m, n->right, fresh);
    refresh(m, root);
    WInt balance = height(m, n->left) - height(m, n->right);
    if (balance > 1) {
        if (m->compare(key, key_at(m, node(m, n->left)->entry)) > 0)
            n->left = left(m, n->left);
        return right(m, root);
    }
    if (balance < -1) {
        if (m->compare(key, key_at(m, node(m, n->right)->entry)) < 0)
            n->right = right(m, n->right);
        return left(m, root);
    }
    return root;
}
void *wd_m_map_edit(void *ctx, WMap **slot, const void *key, WInt insert) {
    WInt found = wd_m_map_find(*slot, key);
    if (found < 0 && !insert) { fail(ctx, "R4011", "dictionary key is absent"); return 0; }
    WMap *m = map_unique(ctx, slot);
    if (!m) return 0;
    WInt size = m->entries->length;
    if (found >= 0) {
        if (!wd_m_seq_unique(ctx, &m->entries, size)) return 0;
        return wd_m_map_entry(m, found, 1);
    }
    if (size == maximum) { fail(ctx, "R4003", "dictionary is too large"); return 0; }
    /* Reserve both arrays before changing logical contents: OOM cannot leave a
       half-inserted entry or an index which does not describe its values. */
    if (!wd_m_seq_unique(ctx, &m->entries, size + 1) ||
        !wd_m_seq_unique(ctx, &m->nodes, size + 1)) return 0;
    void *target = data(m->entries) + size * m->entries->stride;
    copy_bytes(target, key, (WSize)m->key_size);
    if (m->retain_key) m->retain_key(target);
    ++m->entries->length;
    *node(m, size) = (WNode){-1, -1, 1, size};
    ++m->nodes->length;
    m->root = avl_insert(m, m->root, size);
    return wd_m_map_entry(m, size, 1);
}

/* Deletion is O(n), like the reference insertion-order dictionary. Flatten the
   existing AVL index, skip the removed node and build a perfectly balanced
   index in-place. No allocation or user callback occurs after mutation starts. */
static void map_sorted(WMap *m,WInt current,WInt removed,WInt *first,WInt *last) {
    if(current<0) return;
    WInt l=node(m,current)->left,r=node(m,current)->right;
    map_sorted(m,l,removed,first,last);
    if(current!=removed) {
        if(*last<0) *first=current; else node(m,*last)->right=current;
        *last=current;
    }
    map_sorted(m,r,removed,first,last);
}
static WInt map_balanced(WMap *m,WInt *cursor,WInt count) {
    if(!count) return -1;
    WInt l=map_balanced(m,cursor,count/2),root=*cursor;
    *cursor=node(m,root)->right;
    node(m,root)->left=l;
    node(m,root)->right=map_balanced(m,cursor,count-count/2-1);
    refresh(m,root); return root;
}
WInt wd_m_map_remove(void *ctx,WMap **slot,const void *key) {
    if(wd_rt_unwinding(ctx)) return 0;
    WInt removed=wd_m_map_find(*slot,key); if(removed<0) return 0;
    WMap *m=map_unique(ctx,slot); if(!m) return 0;
    WInt size=m->entries->length;
    if(!wd_m_seq_unique(ctx,&m->entries,size) || !wd_m_seq_unique(ctx,&m->nodes,size)) return 0;
    WInt first=-1,last=-1;
    map_sorted(m,m->root,removed,&first,&last);
    if(last>=0) node(m,last)->right=-1;
    m->root=map_balanced(m,&first,size-1);
    unsigned char *entries=data(m->entries); WInt stride=m->entries->stride;
    if(m->entries->drop) m->entries->drop(entries+removed*stride);
    for(WInt i=removed+1;i<size;++i) {
        copy_bytes(entries+(i-1)*stride,entries+i*stride,(WSize)stride);
        *node(m,i-1)=*node(m,i);
    }
    --m->entries->length; --m->nodes->length;
    zero_bytes(entries+(size-1)*stride,(WSize)stride);
    for(WInt i=0;i<size-1;++i) {
        WNode *n=node(m,i); n->entry=i;
        if(n->left>removed) --n->left;
        if(n->right>removed) --n->right;
    }
    if(m->root>removed) --m->root;
    return 1;
}

/* Strict UTF-8 span for diagnostic quoting. Invalid bytes are replaced one at a
   time, matching production's JSON boundary; Wood str itself remains bytes. */
WInt wd_m_utf8_span(const unsigned char *p, WInt remaining) {
    if (remaining <= 0) return 0;
    unsigned a = p[0], n;
    if (a < 0x80) return 1;
    if (a >= 0xc2 && a <= 0xdf) n = 2;
    else if (a >= 0xe0 && a <= 0xef) n = 3;
    else if (a >= 0xf0 && a <= 0xf4) n = 4;
    else return 0;
    if (remaining < (WInt)n) return 0;
    for (unsigned i = 1; i < n; ++i) if (p[i] < 0x80 || p[i] > 0xbf) return 0;
    if ((a == 0xe0 && p[1] < 0xa0) || (a == 0xed && p[1] >= 0xa0) ||
        (a == 0xf0 && p[1] < 0x90) || (a == 0xf4 && p[1] >= 0x90)) return 0;
    return n;
}

#include "callables.inc"
#include "streams.inc"
#include "numbers.inc"
#include "json.inc"
#include "arrays.inc"
#include "math.inc"
#include "format.inc"
#include "services.inc"
#include "random.inc"
#include "driver.inc"
#include "environment.inc"
#include "input_time.inc"
#include "optional.inc"
#include "tasks.inc"
#include "network.inc"
#include "tls.inc"
#include "sqlite.inc"
#include "python.inc"
