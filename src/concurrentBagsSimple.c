#include "concurrentBagsSimple.h"
#include "config.h"

#include <inttypes.h>
#include <omp.h>
#include <stdatomic.h> // gcc -latomic
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef SC
#define CAS(_a, _e, _d) atomic_compare_exchange_weak(_a, _e, _d)
#define LOAD(_a) atomic_load(_a)
#define STORE(_a, _e) atomic_store(_a, _e)
#define FAO(_a, _e) atomic_fetch_or(_a, _e)
#else
#define CAS(_a, _e, _d)                                                        \
  atomic_compare_exchange_weak_explicit(_a, _e, _d, memory_order_acq_rel,      \
                                        memory_order_acquire)
#define LOAD(_a) atomic_load_explicit(_a, memory_order_acquire)
#define STORE(_a, _e) atomic_store_explicit(_a, _e, memory_order_release)
#define FAO(_a, _e) atomic_fetch_or_explicit(_a, _e, memory_order_acq_rel)
#endif

// Initialization variables
int Nr_threads;
// Shared variables
block_t *globalHeadBlock[MAX_NR_THREADS];
// Thread-local storage
block_t *threadBlock, *stealBlock;
bool foundAdd;
int threadHead, stealHead, stealIndex;
int threadID; // Unique number between 0 ... Nr_threads
int numCASSuccess, numCASFail, numSteal;

#pragma omp threadprivate(threadBlock, stealBlock, foundAdd, threadHead,       \
                          stealHead, stealIndex, threadID, numCASSuccess,      \
                          numCASFail, numSteal)

struct block_t {
  DT *_Atomic nodes[MAX_BLOCK_SIZE]; // changed void*
  long _Atomic notifyAdd[MAX_NR_THREADS / WORD_SIZE];
  block_t *_Atomic next;
};

struct bench_result {
  float time;
  int num_items;
  int num_CASSuccess;
  int num_CASFail;
  int num_Steal;
};

void NotifyAll(block_t *block) {
  for (int i = 0; i < (int)(Nr_threads / WORD_SIZE); i++)
    block->notifyAdd[i] = 0;
}

block_t *NewBlock() {
  block_t *block = (block_t *)NewNode(sizeof(block_t));
  block->next = NULL;
  NotifyAll(block);
  for (int i = 0; i < MAX_BLOCK_SIZE; i++)
    block->nodes[i] = NULL;
  return block;
}

void NotifyStart(block_t *block, int Id) {
  long old;
  numCASFail--;
  do {
    old = block->notifyAdd[Id / WORD_SIZE];
    numCASFail++;
  } while (!CAS(&block->notifyAdd[Id / WORD_SIZE], &old,
                old | (1 << (Id % WORD_SIZE))));
  numCASSuccess++;
}

bool NotifyCheck(block_t *block, int Id) {
  return (block->notifyAdd[Id / WORD_SIZE] & (1 << (Id % WORD_SIZE))) == 0;
}

void InitBag(int num_threads) {
  Nr_threads = num_threads;
  for (int i = 0; i < Nr_threads; i++)
    globalHeadBlock[i] = (block_t *){0};
}

void InitThread(int id) {
  threadID = id;
  threadBlock = globalHeadBlock[threadID];
  threadHead = MAX_BLOCK_SIZE;
  stealIndex = 0;
  stealBlock = (block_t *)NULL;
  stealHead = MAX_BLOCK_SIZE;
  numCASSuccess = 0;
  numCASFail = 0;
  numSteal = 0;
}

void Add(void *item) {
  int head = threadHead;
  block_t *block = threadBlock;
  for (;;) {
    if (head == MAX_BLOCK_SIZE) {
      block_t *oldblock = block;
      block = NewBlock();
      block->next = oldblock;
      globalHeadBlock[threadID] = block;
      threadBlock = block;
      head = 0;
    } else if (block->nodes[head] == NULL) {
      NotifyAll(block);
      block->nodes[head] = item;
      threadHead = head + 1;
      return;
    } else
      head++;
  }
}

block_t *NextStealBlock(block_t *cblock) {
  block_t *block = {cblock};
  block_t *next;
  if (block == NULL) {
    block = globalHeadBlock[stealIndex];
  } else {
    next = block->next;
    block = next;
  }
  return block;
}

void *TryStealBlock(int round) {
  int head = stealHead;
  block_t *block = stealBlock;
  foundAdd = false;
  if (block == NULL) {
    block = DeRefLink(&globalHeadBlock[stealIndex]);
    stealBlock = block;
    stealHead = head = 0;
  }
  if (head == MAX_BLOCK_SIZE) {
    stealBlock = block = NextStealBlock(block);
    head = 0;
  }
  if (block == NULL) {
    stealIndex = (stealIndex + 1) % Nr_threads;
    stealHead = 0;
    stealBlock = NULL;
    return NULL;
  }
  if (round == 1)
    NotifyStart(block, threadID);
  else if (round > 1 && NotifyCheck(block, threadID))
    foundAdd = true;
  for (;;) {
    if (head == MAX_BLOCK_SIZE) {
      stealHead = head;
      return NULL;
    } else {
      void *data = block->nodes[head];
      if (data == NULL)
        head++;
      else if (CAS(&block->nodes[head], &data, NULL)) {
        numCASSuccess++;
        stealHead = head;
        return data;
      } else {
        numCASFail++;
      }
    }
  }
}

void *TryRemoveAny() {
  int head = threadHead - 1;
  block_t *block = threadBlock;
  int round = 0;
  for (;;) {
    if (block == NULL || (head < 0 && block->next == NULL)) {
      do {
        int i = 0;
        do {
          numSteal++;
          void *result = TryStealBlock(round);
          if (result != NULL)
            return result;
          if (foundAdd) {
            round = 0;
            i = 0;
          } else if (stealBlock == NULL)
            i++;
        } while (i < Nr_threads);
      } while (++round <= Nr_threads);
      return NULL;
    }
    if (head < 0) {
      block = threadBlock = block->next;
      threadHead = MAX_BLOCK_SIZE - 1;
      head = MAX_BLOCK_SIZE - 1;
    } else {
      DT *data = block->nodes[head];
      if (data != NULL) {
        if (CAS(&block->nodes[head], &data, NULL)) {
          numCASSuccess++;
          threadHead = head;
          return data;
        }
        numCASFail++;
      }
      head--;
    }
  }
}

block_t *DeRefLink(struct block_t *_Atomic *link) { return (block_t *)*link; }

block_t *_Atomic NewNode(int size) {
  block_t _Atomic *new = malloc(size);
  return new;
};

struct bench_result benchmark_add_remove(int num_threads, int num_elems) {
  // First add num_elems elements per thread and then remove them again
  struct bench_result result;
  double tic, toc;

  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  tic = omp_get_wtime();
  {
#pragma omp parallel for
    for (int i = 0; i < num_threads; i++) {
      InitThread(omp_get_thread_num());
    }

#pragma omp barrier

#pragma omp parallel for
    for (int i = 0; i < num_threads; i++) {
      int val = omp_get_thread_num();
      for (int j = 0; j < (int)(num_elems / (num_threads * 2)); j++) {
        Add(&val);
      }
      for (int j = 0; j < (int)(num_elems / (num_threads * 2)); j++) {
        int *res = (int *)(TryRemoveAny());
      }
    }
  }
  toc = omp_get_wtime();

  int cassuc, casfail, steal = 0;
#pragma omp parallel for reduction(+ : cassuc, casfail, steal)
  for (int i = 0; i < num_threads; i++) {
    cassuc += numCASSuccess;
    casfail += numCASFail;
    steal += numSteal;
  }
  result.num_CASSuccess = cassuc;
  result.num_CASFail = casfail;
  result.num_Steal = steal;
  result.time = toc - tic;
  result.num_items = num_threads * (int)(num_elems / num_threads);
  return result;
}

struct bench_result benchmark_random(int num_threads, int num_elems) {
  struct bench_result result;
  double tic, toc;
  srand(1);

  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  tic = omp_get_wtime();
  {
#pragma omp parallel for
    for (int i = 0; i < num_threads; i++) {
      InitThread(omp_get_thread_num());
    }

#pragma omp barrier

#pragma omp parallel for
    for (int i = 0; i < num_threads; i++) {
      int val = omp_get_thread_num();
      for (int j = 0; j < (int)(num_elems / num_threads); j++) {
        if ((float)rand() / (float)(RAND_MAX) < 0.5) {
          Add(&val);
        } else {
          int *res = (int *)(TryRemoveAny());
        }
      }
    }
  }
  toc = omp_get_wtime();

  int cassuc, casfail, steal = 0;
#pragma omp parallel for reduction(+ : cassuc, casfail, steal)
  for (int i = 0; i < num_threads; i++) {
    cassuc += numCASSuccess;
    casfail += numCASFail;
    steal += numSteal;
  }
  result.num_CASSuccess = cassuc;
  result.num_CASFail = casfail;
  result.num_Steal = steal;
  result.time = toc - tic;
  result.num_items = num_threads * (int)(num_elems / num_threads);
  return result;
}

struct bench_result benchmark_half_half(int num_threads, int num_elems) {
  struct bench_result result;
  double tic, toc;
  srand(1);

  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  tic = omp_get_wtime();

#pragma omp parallel for
  for (int i = 0; i < num_threads; i++) {
    InitThread(omp_get_thread_num());
  }

#pragma omp barrier

#pragma omp parallel for
  for (int i = 0; i < num_threads; i++) {
    if (omp_get_thread_num() > (int)(num_threads / 2)) {
      int val = omp_get_thread_num();
      for (int j = 0; j < (int)(num_elems / num_threads); j++) {
        Add(&val);
      }

    } else {
      for (int j = 0; j < (int)(num_elems / num_threads); j++) {
        int *res = (int *)(TryRemoveAny());
      }
    }
  }
  toc = omp_get_wtime();

  int cassuc, casfail, steal = 0;
#pragma omp parallel for reduction(+ : cassuc, casfail, steal)
  for (int i = 0; i < num_threads; i++) {
    cassuc += numCASSuccess;
    casfail += numCASFail;
    steal += numSteal;
  }
  result.num_CASSuccess = cassuc;
  result.num_CASFail = casfail;
  result.num_Steal = steal;
  result.time = toc - tic;
  result.num_items = num_threads * (int)(num_elems / num_threads);
  return result;
}

struct bench_result benchmark_one_producer(int num_threads, int num_elems) {
  struct bench_result result;
  double tic, toc;

  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  tic = omp_get_wtime();

#pragma omp parallel for
  for (int i = 0; i < num_threads; i++) {
    InitThread(omp_get_thread_num());
  }

#pragma omp barrier

#pragma omp parallel for
  for (int i = 0; i < num_threads; i++) {
    if (omp_get_thread_num() == 0) {
      int val = omp_get_thread_num();
      for (int j = 0; j < (int)(num_elems / num_threads); j++) {
        Add(&val);
      }

    } else {
      for (int j = 0; j < (int)(num_elems / num_threads); j++) {
        int *res = (int *)(TryRemoveAny());
      }
    }
  }

  toc = omp_get_wtime();

  int cassuc, casfail, steal = 0;
#pragma omp parallel for reduction(+ : cassuc, casfail, steal)
  for (int i = 0; i < num_threads; i++) {
    cassuc += numCASSuccess;
    casfail += numCASFail;
    steal += numSteal;
  }
  result.num_CASSuccess = cassuc;
  result.num_CASFail = casfail;
  result.num_Steal = steal;
  result.time = toc - tic;
  result.num_items = num_threads * (int)(num_elems / num_threads);
  return result;
}

struct bench_result benchmark_one_consumer(int num_threads, int num_elems) {
  struct bench_result result;
  double tic, toc;

  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  tic = omp_get_wtime();

#pragma omp parallel for
  for (int i = 0; i < num_threads; i++) {
    InitThread(omp_get_thread_num());
  }

#pragma omp barrier

#pragma omp parallel for
  for (int i = 0; i < num_threads; i++) {
    if (omp_get_thread_num() != 0) {
      int val = omp_get_thread_num();
      for (int j = 0; j < (int)(num_elems / num_threads); j++) {
        Add(&val);
      }

    } else {
      for (int j = 0; j < (int)(num_elems / num_threads); j++) {
        int *res = (int *)(TryRemoveAny());
      }
    }
  }

  toc = omp_get_wtime();

  int cassuc, casfail, steal = 0;
#pragma omp parallel for reduction(+ : cassuc, casfail, steal)
  for (int i = 0; i < num_threads; i++) {
    cassuc += numCASSuccess;
    casfail += numCASFail;
    steal += numSteal;
  }
  result.num_CASSuccess = cassuc;
  result.num_CASFail = casfail;
  result.num_Steal = steal;
  result.time = toc - tic;
  result.num_items = num_threads * (int)(num_elems / num_threads);
  return result;
}

void UT_add_remove(int num_threads) {
  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  printf("-------------------------------------\n");
  float tic, toc;
  long genresult = 0;
  int multiply = 100;
  tic = omp_get_wtime();
#pragma omp parallel for reduction(+ : genresult)
  for (int i = 0; i < omp_get_num_threads(); i++) {
    int id = omp_get_thread_num();
    int mult = multiply;
    long result = 0;
    InitThread(id);

    printf("Hello from thread %d\r\n", id + 1);
    int *inc = NULL;

    for (int j = 0; j < mult * 2; j++) {
      if (j < mult) {
        int *item = malloc(sizeof(int));
        *item = 1;
        Add(item);
      } else {
        inc = (int *)(TryRemoveAny());
        if (inc != (int *)NULL) {
          genresult += (long)*inc;
        }
      }
    }
    printf("Thread %d got result %ld\r\n", id + 1, genresult);
  }
  long expected = 0;

  expected = multiply * num_threads;

  printf("Got %ld overall, from %ld possible\r\n", genresult, expected);

  toc = omp_get_wtime();

  printf("Unit test took %lf seconds \r\n", toc - tic);
}

void UT_stealing(int num_threads) {
  omp_set_num_threads(num_threads);
  InitBag(num_threads);
  float tic, toc;
  printf("-------------------------------------\n");

  long genresult = 0;
  int multiply = 100;
  tic = omp_get_wtime();
#pragma omp parallel for reduction(+ : genresult)
  for (int i = 0; i < omp_get_num_threads(); i++) {
    int id = omp_get_thread_num();
    int mult = multiply;
    long result = 0;
    InitThread(id);

    printf("Hello from thread %d\r\n", id + 1);
    int *inc = NULL;

    if (i < num_threads / 2) {
      for (int j = 0; j < mult * (id + 1); j++) {
        int *item = malloc(sizeof(int));
        *item = id + 1;
        Add(item);
      }
    } else {
      int tries = 0;
      do {
        inc = (int *)(TryRemoveAny());
        if (inc != (int *)NULL) {
          result += (long)*inc;
          genresult += (long)*inc;
        }
        tries++;
      } while (tries < 5000);
      printf("Thread %d got result %ld\r\n", id + 1, result);
    }
  }
  long expected = 0;
  for (int i = 0; i < num_threads / 2; i++) {
    expected += multiply * (i + 1) * (i + 1);
  };

  printf("Got %ld overall, from %ld possible\r\n", genresult, expected);

  toc = omp_get_wtime();

  printf("Unit test took %lf seconds \r\n", toc - tic);
}

int main(int argc, char *argv[]) {
  double tic, toc;
  int threads;
  int num_threads;
  int num_elems = 100;
  if (argc == 2) {
    threads = (int)strtol(argv[1], NULL, 10);
  } else
    threads = 4;

  printf("Running with %d threads\r\n", threads);

  UT_add_remove(threads);
  UT_stealing(threads);
}