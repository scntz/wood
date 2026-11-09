/* Erased callable targets are owned values. Concrete closures stay inline in
   generated LLVM; only explicit callable erasure uses this COW allocation.
   Copying a handle is cheap, and mutation detaches the target before invocation.
   Payloads use the private x86-64 ABI's maximum (eight-byte) alignment. */
typedef struct {
    WOwner owner;
    WInt stride;
    WSlot retain, drop;
    WInvoke invoke;
} WCallable;

static void callable_destroy(void *value) {
    WCallable *target = value;
    if (target->drop) target->drop(target + 1);
}
void *wd_m_callable_new(void *ctx, WInt stride, WSlot retain, WSlot drop,
                         WInvoke invoke, const void *value) {
    WInt bytes;
    if (stride < 0 || !invoke || (stride && !value)) {
        fail(ctx,"R4025","invalid native callable target");
        return 0;
    }
    if (!sum(ctx,(WInt)sizeof(WCallable),stride ? stride : 1,&bytes)) return 0;
    WCallable *target = (WCallable *)allocate(ctx,bytes,callable_destroy);
    if (!target) return 0;
    target->stride = stride;
    target->retain = retain;
    target->drop = drop;
    target->invoke = invoke;
    copy_bytes(target + 1,value,(WSize)stride);
    if (retain) retain(target + 1);
    return target;
}
void *wd_m_callable_unique(void *ctx, void **slot) {
    WCallable *target = *slot;
    if (!target) {
        fail(ctx,"R4025","cannot invoke a moved-from callable");
        return 0;
    }
    if (owner_refs(&target->owner) == 1) return target;
    WCallable *next = wd_m_callable_new(ctx,target->stride,target->retain,
        target->drop,target->invoke,target + 1);
    if (!next) return 0; /* The original binding and aliases remain intact. */
    *slot = next;
    wd_m_release(target);
    return next;
}
void *wd_m_callable_data(void *value) { return (WCallable *)value + 1; }
WInvoke wd_m_callable_code(void *value) { return ((WCallable *)value)->invoke; }

/* A link-time marker prevents mixing the expanded emitter/core with old objects. */
void wd_native_runtime_abi4(void) {}
