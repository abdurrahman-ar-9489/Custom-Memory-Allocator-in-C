#ifndef MM_ALLOC_H
#define MM_ALLOC_H
#include <pthread.h> 
#include <stddef.h> 

/* Block header */
typedef struct block_hdr {
    size_t size;                /* payload size */
    int is_free;                /* 1 if free */
    int is_mmap;                /* 1 if block allocated via mmap */
    struct block_hdr *next;     /* next block in overall list */
    struct block_hdr *prev;     /* prev block in overall list */

    /* free-list pointers (doubly linked) */
    struct block_hdr *next_free;
    struct block_hdr *prev_free;
} block_hdr_t;

/* Globals */

static block_hdr_t *all_head = NULL;   /* head of overall block list */
static block_hdr_t *all_tail = NULL;   /* tail of overall block list */
static block_hdr_t *free_head = NULL;  /* head of free list */

/* Alignment */
#define ALIGNMENT 16
#define ALIGN_UP(x)  (((x) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* Threshold to use mmap instead of sbrk (tuneable) */
#define MMAP_THRESHOLD (128 * 1024) /* 128 KB */

/* Minimal payload size to allow splitting */
#define MIN_SPLIT_SIZE 32

/* Global mutex for thread safety */
static pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

/* helpers */
static size_t hdr_size(void);
static void insert_free(block_hdr_t *b);
static void remove_free(block_hdr_t *b);
static void coalesce_with_next(block_hdr_t *b); 
static block_hdr_t *coalesce_with_prev(block_hdr_t *b);
static void split_block(block_hdr_t *b, size_t size);
static void try_release_memory_to_os();
static block_hdr_t *find_free_block(size_t size);
static block_hdr_t *request_space_sbrk(size_t size);
static block_hdr_t *request_mmap(size_t size);

/* main API */
static void *mm_realloc(void *ptr, size_t size);
static void *mm_malloc(size_t size);
static void mm_free(void *ptr);
static void *mm_calloc(size_t nmemb, size_t size);
static void mm_print_state(void);

#endif