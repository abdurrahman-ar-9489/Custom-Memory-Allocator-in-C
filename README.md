🧠 Custom Thread-Safe Memory Allocator in C

A lightweight, educational re-implementation of malloc, calloc, realloc, and free — built from scratch in C, using sbrk() and pthread mutexes for synchronization.
This project provides a low-level understanding of how dynamic memory allocation works under the hood in C libraries like glibc.

🚀 Features

Custom implementation of:

    malloc(size_t size)

    calloc(size_t num, size_t size)

    realloc(void *ptr, size_t size)

    free(void *ptr)

Thread-safe via pthread_mutex_t

Minimal linked-list-based memory management

Coalescing of free blocks (basic version)

Simple debug printing for visualization

Educational use of sbrk() for heap management


🏗️ Software Architecture
🔹 Overview

This allocator maintains a linked list of memory blocks, each represented by a header_t structure that tracks:

Size of the block

Whether it’s free or allocated

Pointer to the next block

When a new memory allocation request comes in:

It first checks the linked list for a free block (get_free_block()).

If none is found, it requests more memory from the OS using sbrk().

The new block is appended to the linked list.

free() marks blocks as reusable or releases them to the OS if they are at the heap end.

⚙️ Architecture Diagram

 ┌──────────────────────────────┐
 │        User Program          │
 │  ─────────────────────────   │
 │  malloc(), calloc(), free()  │
 |______________________________|
                │
                ▼
      ┌───────────────────────┐
      │   Memory Allocator    │
      │  (This Implementation)│
      ├───────────────────────┤
      │   pthread_mutex_lock  │─── Ensures thread-safety
      │   get_free_block()    │─── Finds reusable blocks
      │   sbrk()              │─── Requests memory from OS
      │   free()              │─── Marks block free or releases heap
      │   realloc()           │─── Resizes & copies data
      └──────────┬────────────┘
                 │
                 ▼
         ┌─────────────────┐
         │  Linked List of │
         │ Memory Blocks   │
         ├─────────────────┤
         │ header_t struct │
         │   size          │
         │   is_free       │
         │   next          │
         └────────┬────────┘
                  │
                  ▼
         ┌─────────────────┐
         │  Heap Segment   │
         │ (managed by OS) │
         └─────────────────┘

🧩 Internal Components
1. header_t Structure

Each memory block has metadata stored before the user data:

typedef union header {
    struct {
        size_t size;
        unsigned is_free;
        union header *next;
    } s;
    char align[16]; // ensures 16-byte alignment
} header_t;

2. Global Linked List

head → start of memory block list

tail → last block in list

This allows traversal to find reusable (free) blocks.

3. Key Functions

Function	                                Purpose
malloc(size_t size)	                 Allocates a memory block
free(void *ptr)	                     Frees memory or releases heap end
calloc(size_t n, size_t size)	     Allocates and zeroes memory
realloc(void *ptr, size_t size)	     Resizes a memory block
get_free_block(size_t size)	         Searches for reusable free blocks


⚙️ Build Instructions

🧰 Prerequisites

GCC compiler
Linux/Unix environment (uses sbrk() system call)
pthread library

🧱 Compilation
gcc -pthread custom_malloc.c -o allocator_demo

▶️ Usage Example
#include <stdio.h>
#include "custom_malloc.h"  // if separated into header

int main() {
    int *arr = (int *)malloc(5 * sizeof(int));
    for (int i = 0; i < 5; i++) arr[i] = i * 10;

    for (int i = 0; i < 5; i++) printf("%d ", arr[i]);

    free(arr);
    print_mem_list(); // Debug allocator state
    return 0;
}

Run:

./allocator_demo

🧮 Example Debug Output
head = 0x55f7c... , tail = 0x55f7d...
addr = 0x55f7c..., size = 40, is_free=0, next=0x55f7d...
addr = 0x55f7d..., size = 64, is_free=1, next=(nil)

💡 Design Insights

Aspect	                            Description
Thread Safety	         Achieved using pthread_mutex_lock()
Memory Source	         sbrk() directly manipulates the program break
Linked List Management	 Sequentially searches for free blocks
Block Reuse	             Frees blocks are marked reusable instead of released
Heap Shrinking	         If the freed block is last, the heap is contracted


✅ Advantages

Provides an educational deep dive into how malloc() works.

Thread-safe design for multi-threaded programs.

Demonstrates heap management using sbrk().

Modular and extendable for optimizations (e.g., coalescing, segregated lists).

⚠️ Limitations

Performance: Linear search in linked list for free blocks.

Fragmentation: No block coalescing (adjacent free blocks remain separate).

Thread Safety Caveat: sbrk() itself is not thread-safe in all libc implementations.

Portability: Works primarily on POSIX systems.

No Alignment Guarantees beyond 16-byte boundary.

🧭 Future Improvements

Implement block coalescing for defragmentation

Use mmap() for large allocations

Maintain free list bins (like modern allocators)

Introduce boundary tags for faster merges

Replace sbrk() with arena-based allocatio