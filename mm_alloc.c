/*
 * mm_alloc.c
 *
 * Simple memory allocator with:
 *  - splitting free blocks
 *  - coalescing adjacent free blocks
 *  - mmap() for large allocations
 *  - basic free list management
 *
 * Provides functions:
 *   void *mm_malloc(size_t size);
 *   void  mm_free(void *ptr);
 *   void *mm_calloc(size_t n, size_t size);
 *   void *mm_realloc(void *ptr, size_t size);
 *
 * Not production-grade. Educational and functional.
 */

#define _DEFAULT_SOURCE
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include "mm_alloc.h"


/* helpers */
static size_t hdr_size(void) {
    return ALIGN_UP(sizeof(block_hdr_t));
}

static void insert_free(block_hdr_t *b) {
    b->is_free = 1;
    b->next_free = free_head;
    b->prev_free = NULL;
    if (free_head)
        free_head->prev_free = b;
    free_head = b;
}

static void remove_free(block_hdr_t *b) {
    if (!b) return;
    if (b->prev_free)
        b->prev_free->next_free = b->next_free;
    else
        free_head = b->next_free;
    if (b->next_free)
        b->next_free->prev_free = b->prev_free;
    b->next_free = b->prev_free = NULL;
    b->is_free = 0;
}

/* coalesce b with next if next exists and is free */
static void coalesce_with_next(block_hdr_t *b) {
    block_hdr_t *n = b->next;
    if (!n || !n->is_free || n->is_mmap) return;
    /* remove n from free list */
    remove_free(n);
    /* merge: b.size = b.size + hdr + n.size */
    b->size += hdr_size() + n->size;
    /* unlink n from all list */
    b->next = n->next;
    if (n->next)
        n->next->prev = b;
    else
        all_tail = b;
}

/* coalesce b with prev if prev exists and is free */
static block_hdr_t *coalesce_with_prev(block_hdr_t *b) {
    block_hdr_t *p = b->prev;
    if (!p || !p->is_free || p->is_mmap) return b;
    /* remove b from free list and merge into p */
    remove_free(b);
    remove_free(p);
    p->size += hdr_size() + b->size;
    p->next = b->next;
    if (b->next)
        b->next->prev = p;
    else
        all_tail = p;
    insert_free(p);
    return p;
}

/* request memory from OS via sbrk and append to all list */
static block_hdr_t *request_space_sbrk(size_t size) {
    size_t total = hdr_size() + size;
    void *p = sbrk(total);
    if (p == (void*) -1) return NULL;
    block_hdr_t *hdr = (block_hdr_t *)p;
    hdr->size = size;
    hdr->is_free = 0;
    hdr->is_mmap = 0;
    hdr->next = NULL;
    hdr->prev = all_tail;
    hdr->next_free = hdr->prev_free = NULL;
    if (!all_head)
        all_head = hdr;
    if (all_tail)
        all_tail->next = hdr;
    all_tail = hdr;
    return hdr;
}

/* allocate via mmap */
static block_hdr_t *request_mmap(size_t size) {
    size_t total = hdr_size() + size;
    /* mmap size should be page-aligned; request the total */
    void *p = mmap(NULL, total, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return NULL;
    block_hdr_t *hdr = (block_hdr_t *)p;
    hdr->size = size;
    hdr->is_free = 0;
    hdr->is_mmap = 1;
    hdr->next = hdr->prev = NULL; /* not in global sbrk list */
    hdr->next_free = hdr->prev_free = NULL;
    /* We don't add mmap blocks to all list for sbrk management */
    return hdr;
}

/* find first-fit free block */
static block_hdr_t *find_free_block(size_t size) {
    block_hdr_t *curr = free_head;
    while (curr) {
        if (curr->size >= size)
            return curr;
        curr = curr->next_free;
    }
    return NULL;
}

/* split a big block into two: first part of 'size', leftover becomes free block */
static void split_block(block_hdr_t *b, size_t size) {
    /* only split if leftover would be useful */
    if (b->size < size + hdr_size() + MIN_SPLIT_SIZE) return;
    /* compute address for new header */
    char *payload = (char*)(b + 1);
    char *new_hdr_addr = payload + size;
    block_hdr_t *new_hdr = (block_hdr_t *) new_hdr_addr;
    new_hdr->size = b->size - size - hdr_size();
    new_hdr->is_free = 1;
    new_hdr->is_mmap = 0;
    /* link in all list */
    new_hdr->next = b->next;
    new_hdr->prev = b;
    if (b->next)
        b->next->prev = new_hdr;
    else
        all_tail = new_hdr;
    b->next = new_hdr;
    /* adjust sizes */
    b->size = size;
    /* add new_hdr to free list */
    new_hdr->next_free = new_hdr->prev_free = NULL;
    insert_free(new_hdr);
}

/* shrink heap if the last block(s) are free */
static void try_release_memory_to_os() {
    /* check tail blocks if free and not mmap */
    while (all_tail && all_tail->is_free && !all_tail->is_mmap) {
        block_hdr_t *t = all_tail;
        remove_free(t);
        size_t total = hdr_size() + t->size;
        /* unlink from all list */
        all_tail = t->prev;
        if (all_tail) all_tail->next = NULL;
        else all_head = NULL;
        /* reduce break */
        int ret = brk((char*)t); /* set program break to t (header address) */
        if (ret != 0) {
            /* brk failed; attempt sbrk negative as fallback */
            sbrk(0 - (ssize_t)total);
        }
    }
}

/* API functions */

/* mm_malloc */
void *mm_malloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN_UP(size);
    pthread_mutex_lock(&global_lock);

    /* large allocations via mmap */
    if (size >= MMAP_THRESHOLD) {
        block_hdr_t *hdr = request_mmap(size);
        if (!hdr) {
            pthread_mutex_unlock(&global_lock);
            return NULL;
        }
        void *user = (void*)(hdr + 1);
        pthread_mutex_unlock(&global_lock);
        return user;
    }

    block_hdr_t *b = find_free_block(size);
    if (b) {
        /* remove from free list and possibly split */
        remove_free(b);
        b->is_free = 0;
        /* split if big */
        split_block(b, size);
        void *user = (void*)(b + 1);
        pthread_mutex_unlock(&global_lock);
        return user;
    }

    /* no suitable free block; request via sbrk */
    block_hdr_t *hdr = request_space_sbrk(size);
    if (!hdr) {
        pthread_mutex_unlock(&global_lock);
        return NULL;
    }
    void *user = (void*)(hdr + 1);
    pthread_mutex_unlock(&global_lock);
    return user;
}

/* mm_free */
void mm_free(void *ptr) {
    if (!ptr) return;
    pthread_mutex_lock(&global_lock);
    block_hdr_t *hdr = ((block_hdr_t*)ptr) - 1;
    if (hdr->is_mmap) {
        /* unmap the whole region */
        size_t total = hdr_size() + hdr->size;
        munmap((void*)hdr, total);
        pthread_mutex_unlock(&global_lock);
        return;
    }
    /* mark free and insert into free list */
    insert_free(hdr);
    /* coalesce forward */
    coalesce_with_next(hdr);
    /* coalesce backward (returns new head of coalesced block) */
    hdr = coalesce_with_prev(hdr);
    /* possibly release memory to OS if tail */
    try_release_memory_to_os();
    pthread_mutex_unlock(&global_lock);
}

/* mm_calloc */
void *mm_calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;
    /* check overflow */
    if (size != 0 && nmemb > (size_t)-1 / size) return NULL;
    size_t total = nmemb * size;
    void *p = mm_malloc(total);
    if (!p) return NULL;
    memset(p, 0, total);
    return p;
}

/* mm_realloc */
void *mm_realloc(void *ptr, size_t size) {
    if (!ptr) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }
    size = ALIGN_UP(size);
    pthread_mutex_lock(&global_lock);
    block_hdr_t *hdr = ((block_hdr_t*)ptr) - 1;
    /* if mmap block and new size <= old, maybe shrink (don't remap) */
    if (hdr->is_mmap) {
        if (hdr->size >= size) {
            pthread_mutex_unlock(&global_lock);
            return ptr;
        } else {
            /* allocate new (mmap or sbrk), copy, free old */
            pthread_mutex_unlock(&global_lock);
            void *newp = mm_malloc(size);
            if (!newp) return NULL;
            memcpy(newp, ptr, hdr->size);
            mm_free(ptr);
            return newp;
        }
    }

    if (hdr->size >= size) {
        /* optionally split the block */
        split_block(hdr, size);
        pthread_mutex_unlock(&global_lock);
        return ptr;
    }

    /* try to expand into next if free and not mmap */
    block_hdr_t *next = hdr->next;
    if (next && next->is_free && !next->is_mmap &&
        (hdr->size + hdr_size() + next->size) >= size) {
        /* merge with next */
        remove_free(next);
        hdr->size += hdr_size() + next->size;
        hdr->next = next->next;
        if (next->next)
            next->next->prev = hdr;
        else
            all_tail = hdr;
        /* now split if too large */
        split_block(hdr, size);
        pthread_mutex_unlock(&global_lock);
        return ptr;
    }

    /* allocate new block */
    pthread_mutex_unlock(&global_lock);
    void *newp = mm_malloc(size);
    if (!newp) return NULL;
    /* copy old data */
    memcpy(newp, ptr, hdr->size);
    mm_free(ptr);
    return newp;
}

/* Debug: print lists */
void mm_print_state(void) {
    pthread_mutex_lock(&global_lock);
    printf("All blocks:\n");
    block_hdr_t *b = all_head;
    while (b) {
        printf("  [%p] size=%zu free=%d mmap=%d next=%p prev=%p\n",
               (void*)b, b->size, b->is_free, b->is_mmap, (void*)b->next, (void*)b->prev);
        b = b->next;
    }
    printf("Free list:\n");
    b = free_head;
    while (b) {
        printf("  [%p] size=%zu\n", (void*)b, b->size);
        b = b->next_free;
    }
    pthread_mutex_unlock(&global_lock);
}

/* Example usage test main (compile with -DMM_ALLOC_TEST to enable) */
#ifdef MM_ALLOC_TEST
#include <stdlib.h>
int main(void) {
    void *p1 = mm_malloc(100);
    void *p2 = mm_malloc(200);
    mm_print_state();
    mm_free(p1);
    mm_print_state();
    void *p3 = mm_malloc(50);
    mm_print_state();
    p2 = mm_realloc(p2, 400);
    mm_print_state();
    void *big = mm_malloc(200000); /* triggers mmap */
    mm_print_state();
    mm_free(big);
    mm_print_state();
    return 0;
}
#endif
