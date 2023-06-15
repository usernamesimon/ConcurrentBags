#include "concurrentBags.h"
#include <omp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>

struct bench_result {
  float time;
  int num_items;
};

struct bench_result benchmark_add_remove(int num_threads, int num_elems) {
  // First add num_elems elements per thread and then remove them again
  struct bench_result result;
  double tic, toc;

  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  tic = omp_get_wtime();
  {
#pragma omp parallel for private(threadBlock, stealBlock, stealPrev, foundAdd, \
                                 threadHead, stealHead, stealIndex, threadID)
    for (int i = 0; i < num_threads; i++) {
      InitThread(omp_get_thread_num());
    }

#pragma omp barrier

#pragma omp parallel for private(threadBlock, stealBlock, stealPrev, foundAdd, \
                                 threadHead, stealHead, stealIndex, threadID)
    for (int i = 0; i < num_threads; i++) {
      int val = omp_get_thread_num();
      for (int j = 0; j < num_elems; j++) {
        Add(&val);
      }
      for (int j = 0; j < num_elems; j++) {
        int *res = (int *)(TryRemoveAny());
      }
    }
  }
  toc = omp_get_wtime();

  result.time = toc - tic;
  result.num_items = num_threads * num_elems;
  return result;
}

struct bench_result benchmark_random(int num_threads, int num_elems) {
  struct bench_result result;
  double tic, toc;
  srand(time());

  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  tic = omp_get_wtime();
  {
#pragma omp parallel for private(threadBlock, stealBlock, stealPrev, foundAdd, \
                                 threadHead, stealHead, stealIndex, threadID)
    for (int i = 0; i < num_threads; i++) {
      InitThread(omp_get_thread_num());
    }

#pragma omp barrier

#pragma omp parallel for private(threadBlock, stealBlock, stealPrev, foundAdd, \
                                 threadHead, stealHead, stealIndex, threadID)
    for (int i = 0; i < num_threads; i++) {
      int val = omp_get_thread_num();
      for (int j = 0; j < num_elems; j++) {
        if ((float)
          rand() / (float)(RAND_MAX) < 0.5 ){ Add(&val); }
        else {
          int *res = (int *)(TryRemoveAny());
        }
      }
    }
  }
  toc = omp_get_wtime();

  result.time = toc - tic;
  result.num_items = num_threads * num_elems;
  return result;
}

struct bench_result benchmark_half_half {
  struct bench_result result;
  double tic, toc;
  srand(time());

  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  tic = omp_get_wtime();
  {
#pragma omp parallel for private(threadBlock, stealBlock, stealPrev, foundAdd, \
                                 threadHead, stealHead, stealIndex, threadID)
    for (int i = 0; i < num_threads; i++) {
      InitThread(omp_get_thread_num());
    }

#pragma omp barrier

#pragma omp parallel for private(threadBlock, stealBlock, stealPrev, foundAdd, \
                                 threadHead, stealHead, stealIndex, threadID)
    for (int i = 0; i < num_threads; i++) {
      if (i < int(num_threads / 2)) {
        int val = omp_get_thread_num();
        for (int j = 0; j < num_elems; j++) {
          Add(&val);
        }

      } else {
        for (int j = 0; j < num_elems; j++) {
          int *res = (int *)(TryRemoveAny());
        }
      }
    }
  }
  toc = omp_get_wtime();

  result.time = toc - tic;
  result.num_items = num_threads * num_elems;
  return result;
}

struct bench_result benchmark_one_producer{
  struct bench_result result;
  double tic, toc;
  srand(time());

  omp_set_num_threads(num_threads);
  InitBag(num_threads);

  tic = omp_get_wtime();
  {
#pragma omp parallel for private(threadBlock, stealBlock, stealPrev, foundAdd, \
                                 threadHead, stealHead, stealIndex, threadID)
    for (int i = 0; i < num_threads; i++) {
      InitThread(omp_get_thread_num());
    }

#pragma omp barrier

#pragma omp parallel for private(threadBlock, stealBlock, stealPrev, foundAdd, \
                                 threadHead, stealHead, stealIndex, threadID)
    for (int i = 0; i < num_threads; i++) {
      if (i == 0) {
        int val = omp_get_thread_num();
        for (int j = 0; j < num_elems; j++) {
          Add(&val);
        }

      } else {
        for (int j = 0; j < num_elems; j++) {
          int *res = (int *)(TryRemoveAny());
        }
      }
    }
  }
  toc = omp_get_wtime();

  result.time = toc - tic;
  result.num_items = num_threads * num_elems;
  return result;
}
