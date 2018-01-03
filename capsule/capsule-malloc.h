#pragma once

#define CAPSULE_MALLOC_EXTRA_CHECKS

#define DEBUG_MALLOC_VOODOO 0

#if DEBUG_MALLOC_VOODOO
#include <stdio.h>
#include <sys/param.h>
// can't printf inside malloc/free et al
// can't jump to an fputs from a different libc either
typedef int (*fputsfunc) (const char *buf, FILE *s);
static fputsfunc wf = NULL;

#define CAN_DEBUG_ALLOCS (wf != NULL)

static void dump_ptr (const char *label, const void *ptr)
{
    char ptrbuf[ (sizeof(void *) * 2) + 1 ];
    unsigned long x = (unsigned long) ptr;
    static const char pattern[] = "0123456789abcdef";
    ssize_t start = sizeof(long) * 2;

    ptrbuf[ sizeof(ptrbuf) - 1 ] = '\0';

    for( size_t i = 0; i < sizeof(long) * 2; i++ )
    {
        size_t c = (x >> ((((sizeof(long) * 2) - 1) - i) * 4)) & 0b01111;
        ptrbuf[ i ] = pattern[ c ];
        if( (start == (sizeof(long) * 2)) && (ptrbuf[i] != '0') )
            start = i;
    }

    start = MAX( 0, start - 1 );

    wf( label  , stderr );
    wf( ": <0x", stderr );
    wf( ptrbuf + start, stderr );
    wf( ">\n"  , stderr );
}
#define FETCH_FPUTS() \
    if(UNLIKELY(!wf)) wf = (fputsfunc) dlsym(cap->dl_handle, "fputs")
#else
#define dump_ptr(a,b)
#define FETCH_FPUTS()
#define CAN_DEBUG_ALLOCS 0
#endif


////////////////////////////////////////////////////////////////////////////
// copy some vodoo out of libc.
// it is to be hoped that this is a temporary hack but, well…

#define SIZE_SZ (sizeof(size_t))

struct malloc_chunk
{
  size_t               prev_size;  /* Size of previous chunk (if free).  */
  size_t               size;       /* Size in bytes, including overhead. */

  struct malloc_chunk* fd;         /* double links -- used only if free. */
  struct malloc_chunk* bk;

  struct malloc_chunk* fd_nextsize; /* double links -- used only if free. */
  struct malloc_chunk* bk_nextsize;
};

typedef struct malloc_chunk* mchunkptr;

#define chunk2mem(p)   ((void*)((char*)(p) + 2*SIZE_SZ))
#define mem2chunk(mem) ((mchunkptr)((char*)(mem) - 2*SIZE_SZ))

/* size field is or'ed with IS_MMAPPED if the chunk was obtained with mmap() */
#define IS_MMAPPED     0x2

#define chunk_is_mmapped(p)     ((p)->size & IS_MMAPPED)

////////////////////////////////////////////////////////////////////////////
// extra voodoo. not clear if we need this:
#ifdef CAPSULE_MALLOC_EXTRA_CHECKS

#define PREV_INUSE     0x1
/* similarly for non-main-arena chunks */
#define NON_MAIN_ARENA 0x4

#define chunk_non_main_arena(p) ((p)->size & NON_MAIN_ARENA)

typedef struct malloc_chunk *mfastbinptr;
typedef int mutex_t;

#define fastbin(ar_ptr, idx) ((ar_ptr)->fastbinsY[idx])
/* offset 2 to use otherwise unindexable first 2 bins */
#define fastbin_index(sz) \
  ((((unsigned int) (sz)) >> (SIZE_SZ == 8 ? 4 : 3)) - 2)
/* The maximum fastbin request size we support */
#define MAX_FAST_SIZE  (80 * SIZE_SZ / 4)

#define MALLOC_ALIGNMENT  (2 *SIZE_SZ < __alignof__ (long double) ? \
                           __alignof__ (long double) : 2 *SIZE_SZ)
#define MALLOC_ALIGN_MASK (MALLOC_ALIGNMENT - 1)
#define MIN_CHUNK_SIZE    (offsetof(struct malloc_chunk, fd_nextsize))
/* The smallest size we can malloc is an aligned minimal chunk */
#define MINSIZE  \
  (unsigned long)(((MIN_CHUNK_SIZE+MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK))

#define request2size(req)                                         \
  (((req) + SIZE_SZ + MALLOC_ALIGN_MASK < MINSIZE)  ?             \
   MINSIZE :                                                      \
   ((req) + SIZE_SZ + MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK)

#define NFASTBINS   (fastbin_index (request2size (MAX_FAST_SIZE)) + 1)
#define NBINS       128
#define BINMAPSHIFT 5
#define BITSPERMAP  (1U << BINMAPSHIFT)
#define BINMAPSIZE  (NBINS / BITSPERMAP)

struct malloc_state
{
  /* Serialize access.  */
  mutex_t mutex;

  /* Flags (formerly in max_fast).  */
  int flags;

  /* Fastbins */
  mfastbinptr fastbinsY[NFASTBINS];

  /* Base of the topmost chunk -- not otherwise kept in a bin */
  mchunkptr top;

  /* The remainder from the most recent split of a small request */
  mchunkptr last_remainder;

  /* Normal bins packed as described above */
  mchunkptr bins[NBINS * 2 - 2];

  /* Bitmap of bins */
  unsigned int binmap[BINMAPSIZE];

  /* Linked list */
  struct malloc_state *next;

  /* Linked list for free arenas.  Access to this field is serialized
     by free_list_lock in arena.c.  */
  struct malloc_state *next_free;

  /* Number of threads attached to this arena.  0 if the arena is on
     the free list.  Access to this field is serialized by
     free_list_lock in arena.c.  */
  size_t attached_threads;

  /* Memory allocated from the system in this arena.  */
  size_t system_mem;
  size_t max_system_mem;
};

typedef struct malloc_state *mstate;

typedef struct _heap_info
{
  mstate ar_ptr; /* Arena for this heap. */
  struct _heap_info *prev; /* Previous heap. */
  size_t size;   /* Current size in bytes. */
  size_t mprotect_size; /* Size in bytes that has been mprotected
                           PROT_READ|PROT_WRITE.  */
  /* Make sure the following data is properly aligned, particularly
     that sizeof (heap_info) + 2 * SIZE_SZ is a multiple of
     MALLOC_ALIGNMENT. */
  char pad[-6 * SIZE_SZ & MALLOC_ALIGN_MASK];
} heap_info;

#ifndef DEFAULT_MMAP_THRESHOLD_MAX
  /* For 32-bit platforms we cannot increase the maximum mmap
     threshold much because it is also the minimum value for the
     maximum heap size and its alignment.  Going above 512k (i.e., 1M
     for new heaps) wastes too much address space.  */
# if __WORDSIZE == 32
#  define DEFAULT_MMAP_THRESHOLD_MAX (512 * 1024)
# else
#  define DEFAULT_MMAP_THRESHOLD_MAX (4 * 1024 * 1024 * sizeof(long))
# endif
#endif

#define HEAP_MIN_SIZE (32 * 1024)
#ifndef HEAP_MAX_SIZE
# ifdef DEFAULT_MMAP_THRESHOLD_MAX
#  define HEAP_MAX_SIZE (2 * DEFAULT_MMAP_THRESHOLD_MAX)
# else
#  define HEAP_MAX_SIZE (1024 * 1024)
# endif
#endif

#define heap_for_ptr(ptr) \
  ((heap_info *) ((unsigned long) (ptr) & ~(HEAP_MAX_SIZE - 1)))
#define arena_for_chunk(ptr) \
  (chunk_non_main_arena (ptr) ? heap_for_ptr (ptr)->ar_ptr : NULL)

#define chunk_at_offset(p, s)  ((mchunkptr) (((char *) (p)) + (s)))

typedef struct malloc_state *mstate;

#define NONCONTIGUOUS_BIT (2U)
#define contiguous(M)     (((M)->flags & NONCONTIGUOUS_BIT) == 0)
#define noncontiguous(M)  (((M)->flags & NONCONTIGUOUS_BIT) != 0)

#define SIZE_BITS    (PREV_INUSE | IS_MMAPPED | NON_MAIN_ARENA)
#define chunksize(p) ((p)->size & ~(SIZE_BITS))

#endif // CAPSULE_MALLOC_EXTRA_CHECKS
////////////////////////////////////////////////////////////////////////////
