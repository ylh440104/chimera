typedef struct {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *);
    struct { unsigned long reserved, size; void (*copy)(void *, const void *); void (*dispose)(void *); } *descriptor;
} runtime_block;

typedef struct runtime_byref {
    void *isa;
    struct runtime_byref *forwarding;
    int flags, size;
} runtime_byref;

typedef struct block_allocation {
    void *object;
    unsigned references;
    int byref;
    struct block_allocation *next;
} block_allocation;

static block_allocation *block_allocations;

static block_allocation *block_find(const void *object)
{
    for (block_allocation *a = block_allocations; a; a = a->next)
        if (a->object == object) return a;
    return NULL;
}

static void *block_allocate(size_t size, int byref)
{
    if (size < (byref ? sizeof(runtime_byref) : sizeof(runtime_block)) || size > 1024 * 1024) return NULL;
    block_allocation *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->object = malloc(size);
    if (!a->object) { free(a); return NULL; }
    a->references = 1; a->byref = byref; a->next = block_allocations; block_allocations = a;
    return a->object;
}

void *chm_block_copy(void *object)
{
    if (!object) return NULL;
    runtime_block *b = object;
    block_allocation *a = block_find(object);
    if (a) { a->references++; return object; }
    if (b->flags & (1 << 28)) return object;
    if (!b->descriptor || !b->invoke) return NULL;
    runtime_block *copy = block_allocate(b->descriptor->size, 0);
    if (!copy) return NULL;
    memcpy(copy, b, b->descriptor->size);
    copy->flags |= 1 << 24;
    if ((b->flags & (1 << 25)) && b->descriptor->copy) b->descriptor->copy(copy, b);
    return copy;
}

void chm_block_release(void *object)
{
    block_allocation **link = &block_allocations;
    while (*link && (*link)->object != object) link = &(*link)->next;
    if (!*link) return;
    block_allocation *a = *link;
    if (--a->references) return;
    *link = a->next;
    if (a->byref) {
        runtime_byref *b = object;
        if (b->flags & (1 << 25)) {
            void (**helpers)(void *) = (void (**)(void *))(b + 1);
            if (helpers[1]) helpers[1](b);
        }
    } else {
        runtime_block *b = object;
        if ((b->flags & (1 << 25)) && b->descriptor->dispose) b->descriptor->dispose(b);
    }
    free(object); free(a);
}

static void block_object_assign_impl(void *dst, void *src, int flags)
{
    if (!dst) return;
    if (flags & 8) {
        runtime_byref *b = src;
        if (!b) { *(void **)dst = NULL; return; }
        b = b->forwarding;
        block_allocation *a = block_find(b);
        if (a) a->references++;
        else {
            runtime_byref *copy = block_allocate((size_t)b->size, 1);
            if (!copy) { *(void **)dst = NULL; return; }
            memcpy(copy, b, (size_t)b->size);
            copy->forwarding = copy; copy->flags |= 1 << 24;
            b->forwarding = copy;
            block_find(copy)->references = 2;
            if (b->flags & (1 << 25)) {
                void (**helper)(void *, void *) = (void (**)(void *, void *))(b + 1);
                if (*helper) (*helper)(copy, b);
            }
            b = copy;
        }
        *(void **)dst = b;
    } else if ((flags & 15) == 7 && !(flags & 128)) *(void **)dst = chm_block_copy(src);
    else *(void **)dst = src;
}

static void block_object_dispose_impl(void *object, int flags)
{
    if (flags & 8) {
        runtime_byref *b = object;
        if (b) chm_block_release(b->forwarding);
    } else if ((flags & 15) == 7 && !(flags & 128)) chm_block_release(object);
}

void chm_blocks_reset(void)
{
    for (;;) {
        block_allocation *a = block_allocations;
        while (a && a->byref) a = a->next;
        if (!a) break;
        a->references = 1;
        chm_block_release(a->object);
    }
    while (block_allocations) {
        block_allocation *a = block_allocations;
        block_allocations = a->next;
        free(a->object); free(a);
    }
}

static void copy_weak_impl(void **dst, void **src)
{
    if (dst) *dst = src ? *src : NULL;
}

static void move_weak_impl(void **dst, void **src)
{
    copy_weak_impl(dst, src);
    if (src) *src = NULL;
}
