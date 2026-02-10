//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// minialloc - A minimal thread-safe memory allocator.
//
// Design: implicit free list with first-fit search. Each block has a header
// with a prev pointer and a size|used field. Blocks are contiguous in memory;
// the next block is found by address arithmetic. Both forward and backward
// coalescing are supported. Thread safety via a spinlock using stdatomic.
// Memory obtained from the OS via sbrk or brk.

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef unsigned long uptr;
typedef long iptr;

_Static_assert(sizeof(uintptr_t) == sizeof(void *), "incorrect uintptr_t size");
_Static_assert(sizeof(intptr_t) == sizeof(void *), "incorrect intptr_t size");
_Static_assert(sizeof(uptr) == sizeof(void *), "incorrect uptr size");
_Static_assert(sizeof(iptr) == sizeof(void *), "incorrect iptr size");

#ifndef OVERRIDE_SYSTEM_ALLOCATOR
#define OVERRIDE_SYSTEM_ALLOCATOR 1
#endif

#ifdef MINIALLOC_USE_SBRK
#include <unistd.h>
#define mini_sbrk sbrk
#else
# ifndef BRK
# error "no brk implementation provided"
# endif

extern void *BRK(void *addr);

static void *mini_sbrk(iptr increment) {
    static void *cur_brk;
    if (!cur_brk)
        cur_brk = BRK(0);
    if (increment == 0)
        return cur_brk;
    void *new_brk = (char *)cur_brk + increment;
    if (BRK(new_brk) != new_brk)
        return (void *)-1;
    void *old_brk = cur_brk;
    cur_brk = new_brk;
    return old_brk;
}
#endif

#define ALIGN _Alignof(max_align_t)
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define MIN_SBRK 1024 // minimum bytes to request from sbrk

struct block {
    struct block *prev; // previous block (NULL if first)
    size_t info;        // payload size | used flag (bit 0)
};

static inline size_t blk_size(struct block *b) { return b->info & ~(size_t)1; }
static inline int blk_used(struct block *b) { return b->info & 1; }
static inline void blk_set(struct block *b, size_t size, int used) {
    b->info = size | (used ? (size_t)1 : 0);
}
static inline struct block *blk_next(struct block *b) {
    return (struct block *)((char *)(b + 1) + blk_size(b));
}

static void *heap_start;
static void *heap_end;
static struct block *heap_last;
static atomic_flag lock = ATOMIC_FLAG_INIT;

static inline void cpu_relax(void) {
#if defined(__x86_64__)
    __asm__ volatile("pause");
#elif defined(__aarch64__)
    __asm__ volatile("yield");
#endif
}

static void spin_lock(void) {
    while (atomic_flag_test_and_set_explicit(&lock, memory_order_acquire))
        cpu_relax();
}

static void spin_unlock(void) {
    atomic_flag_clear_explicit(&lock, memory_order_release);
}

void *mini_malloc(size_t size) {
    if (size == 0 || size > INTPTR_MAX - sizeof(struct block) - ALIGN)
        return NULL;
    size = ALIGN_UP(size, ALIGN);

    spin_lock();

    if (!heap_start) {
        heap_start = mini_sbrk(0);
        heap_end = heap_start;
    }

    // First-fit search.
    struct block *b = heap_start;
    while ((uptr)b < (uptr)heap_end) {
        if (!blk_used(b) && blk_size(b) >= size) {
            // Split if the remainder can hold a block.
            if (blk_size(b) >= size + sizeof(struct block) + ALIGN) {
                size_t old_size = blk_size(b);
                blk_set(b, size, 1);
                struct block *split = blk_next(b);
                split->prev = b;
                blk_set(split, old_size - size - sizeof(struct block), 0);
                struct block *after = blk_next(split);
                if ((uptr)after < (uptr)heap_end)
                    after->prev = split;
                if (b == heap_last)
                    heap_last = split;
            } else {
                blk_set(b, blk_size(b), 1);
            }
            spin_unlock();
            return b + 1;
        }
        b = blk_next(b);
    }

    // No fit found: extend the heap.
    size_t total = sizeof(struct block) + size;
    if (total < MIN_SBRK)
        total = MIN_SBRK;
    void *p = mini_sbrk((iptr)total);
    if (p == (void *)-1) {
        spin_unlock();
        return NULL;
    }
    b = p;
    b->prev = heap_last;
    blk_set(b, size, 1);
    heap_last = b;
    heap_end = (char *)p + total;

    // Split the excess into a free block.
    size_t remainder = total - sizeof(struct block) - size;
    if (remainder >= sizeof(struct block) + ALIGN) {
        blk_set(b, size, 1);
        struct block *split = blk_next(b);
        split->prev = b;
        blk_set(split, remainder - sizeof(struct block), 0);
        heap_last = split;
    } else {
        blk_set(b, total - sizeof(struct block), 1);
    }

    spin_unlock();
    return b + 1;
}

void mini_free(void *ptr) {
    if (!ptr)
        return;

    spin_lock();

    struct block *b = (struct block *)ptr - 1;
    blk_set(b, blk_size(b), 0);

    // Coalesce forward: absorb all consecutive free blocks.
    struct block *next = blk_next(b);
    while ((uptr)next < (uptr)heap_end && !blk_used(next)) {
        if (next == heap_last)
            heap_last = b;
        blk_set(b, blk_size(b) + sizeof(struct block) + blk_size(next), 0);
        next = blk_next(b);
    }
    if ((uptr)next < (uptr)heap_end)
        next->prev = b;

    // Coalesce backward: merge into previous block if free.
    if (b->prev && !blk_used(b->prev)) {
        struct block *prev = b->prev;
        if (b == heap_last)
            heap_last = prev;
        blk_set(prev, blk_size(prev) + sizeof(struct block) + blk_size(b), 0);
        next = blk_next(prev);
        if ((uptr)next < (uptr)heap_end)
            next->prev = prev;
    }

    spin_unlock();
}

void *mini_calloc(size_t nmemb, size_t size) {
    if (nmemb && size > SIZE_MAX / nmemb)
        return NULL;
    size_t total = nmemb * size;
    void *p = mini_malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void *mini_realloc(void *ptr, size_t size) {
    if (!ptr)
        return mini_malloc(size);
    if (size == 0) {
        mini_free(ptr);
        return NULL;
    }

    if (size > INTPTR_MAX - sizeof(struct block) - ALIGN)
        return NULL;

    struct block *b = (struct block *)ptr - 1;
    if (blk_size(b) >= ALIGN_UP(size, ALIGN))
        return ptr;

    void *new = mini_malloc(size);
    if (!new)
        return NULL;
    memcpy(new, ptr, blk_size(b));
    mini_free(ptr);
    return new;
}

#if OVERRIDE_SYSTEM_ALLOCATOR
void *malloc(size_t size) { return mini_malloc(size); }
void *realloc(void *ptr, size_t size) { return mini_realloc(ptr, size); }
void *calloc(size_t nmemb, size_t size) { return mini_calloc(nmemb, size); }
void free(void *ptr) { mini_free(ptr); }
#endif
