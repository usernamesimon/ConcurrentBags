#include "concurrentBags.h"
#include "memoryManagement.h"
#include "config.h"

#include <stdatomic.h> // gcc -latomic
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>

#include <assert.h>

#ifdef SC
#define CAS(_a, _e, _d) atomic_compare_exchange_weak(_a, _e, _d)
#define LOAD(_a) atomic_load(_a)
#define STORE(_a, _e) atomic_store(_a, _e)
#define FAO(_a, _e) atomic_fetch_or(_a, _e)
#else
#define CAS(_a, _e, _d) atomic_compare_exchange_weak_explicit(_a, _e, _d, memory_order_acq_rel, memory_order_acquire)
#define LOAD(_a) atomic_load_explicit(_a, memory_order_acquire)
#define STORE(_a, _e) atomic_store_explicit(_a, _e, memory_order_release)
#define FAO(_a, _e) atomic_fetch_or_explicit(_a, _e, memory_order_acq_rel)
#endif

#define UNMARK_MASK ~0b11
#define MARK_BIT1 0b01
#define MARK_BIT2 0b10

#define getpointer(_markedpointer) ((block_t *)(((long)_markedpointer) & UNMARK_MASK))
#define ismarked1(_markedpointer) ((((uintptr_t)_markedpointer) & MARK_BIT1) != 0x0)
#define setmark1(_markedpointer) ((block_t *)(((long)_markedpointer) | MARK_BIT1))
#define ismarked2(_markedpointer) ((((uintptr_t)_markedpointer) & MARK_BIT2) != 0x0)
#define setmark2(_markedpointer) ((block_t *)(((long)_markedpointer) | MARK_BIT2))


// Shared variables
block_t * _Atomic globalHeadBlock[MAX_NR_THREADS];

volatile block_t *threadBlock, *stealBlock, *stealPrev;
bool foundAdd;
int threadHead, stealHead, stealIndex;
int threadID; // Unique number between 0 ... Nr_threads

#pragma omp threadprivate(threadBlock, stealBlock, stealPrev, foundAdd, threadHead, stealHead, stealIndex, threadID)

struct block_t
{
    DT * _Atomic nodes[MAX_BLOCK_SIZE]; // changed void*
    long _Atomic notifyAdd[MAX_NR_THREADS / WORD_SIZE];
    /* Attention! Also holds marked1 and marked2 in its lsb
    Therefore have to mask when actually dereferencing the pointer
    */
    block_t* _Atomic next;
};

void Mark1Block(block_t *block)
{
    for (;;)
    {
        if(getpointer(block) == NULL) break;
        block_t* next = getpointer(block)->next;
        block_t* new = setmark1(next);

        if (getpointer(next) == NULL ||
            ismarked1(next) ||
            CAS(&getpointer(block)->next, &next, new))
            break;
    }
}

void NotifyAll(block_t *block)
{
    for (int i = 0; i < (int)(MAX_NR_THREADS / WORD_SIZE); i++)
        block->notifyAdd[i] = 0;
}

block_t* _Atomic NewBlock()
{
    block_t* _Atomic block = NewNode();
    block_t* work = block;
    NotifyAll(work);
    for (int i = 0; i < MAX_BLOCK_SIZE; i++)
        work->nodes[i] = NULL;
    return block;
}

void NotifyStart(block_t *block, int Id)
{
    long old;
    do
    {
        old = block->notifyAdd[Id / WORD_SIZE];
    } while (!CAS(&block->notifyAdd[Id / WORD_SIZE], &old, old | (1 << (Id % WORD_SIZE))));
}

bool NotifyCheck(block_t *block, int Id)
{
    return (block->notifyAdd[Id / WORD_SIZE] & (1 << (Id % WORD_SIZE))) == 0;
}

void InitBag(int num_threads)
{
    /*
    Nr_threads = num_threads;
    block_t* hb[MAX_NR_THREADS];
    globalHeadBlock = hb;
    */
    for (int i = 0; i < MAX_NR_THREADS; i++)
        globalHeadBlock[i] = NewBlock();
}

void InitThread(int id)
{
    threadID = id;
    threadBlock = globalHeadBlock[threadID];
    threadHead = MAX_BLOCK_SIZE;
    stealIndex = 0;
    stealBlock = (block_t *)NULL;
    stealPrev = (block_t *)NULL;
    stealHead = MAX_BLOCK_SIZE;
}

void Add(void *item)
{
    int head = threadHead;
    block_t *block = threadBlock;
    for (;;)
    {
        if (head == MAX_BLOCK_SIZE)
        {
            block_t *oldblock = block;
            block = NewBlock();
            block_t* new = getpointer(oldblock); //equivalent to setting flags to false
            block->next = new; //? true, true?
            globalHeadBlock[threadID] = block;
            threadBlock = block;
            head = 0;
        }
        else if (block->nodes[head] == NULL)
        {
            NotifyAll(block);
            block->nodes[head] = item;
            threadHead = head + 1;
            return;
        }
        else
            head++;
    }
}

block_t *NextStealBlock(volatile block_t *block)
{
    volatile block_t* next;
    for (;;)
    {
        if (block == NULL)
        {
            block = DeRefLink(&globalHeadBlock[stealIndex]);
            break;
        }
        next = DeRefLink(&block->next);
        if (ismarked2(next)) Mark1Block(next); // next.p or next?
        if (stealPrev == NULL || getpointer(next) == NULL)
        {
            if (ismarked1(next))
            {
                if (getpointer(next) != NULL) NotifyAll(getpointer(next));
                if (CAS(&globalHeadBlock[stealIndex], &block, getpointer(next)))
                {
                    block->next = setmark1((block_t*){NULL});
                    DeleteNode(block);
                    ReScan(next);
                }
                else
                {
                    stealPrev = NULL;
                    block = DeRefLink(&globalHeadBlock[stealIndex]);
                    continue;
                }
            }
            else
                stealPrev = block;
        }
        else
        {
            if (ismarked1(next))
            {
                block_t* copy = stealPrev->next;
                block_t* prevnext = (block_t*)getpointer(block);
                if (ismarked2(copy)) prevnext = setmark2(prevnext);
                block_t* new = (block_t*){next};
                
                if (CAS(&stealPrev->next, &prevnext, new))
                {
                    block->next = setmark2(NULL);
                    DeleteNode(block);
                    ReScan(next);
                }
                else
                {
                    stealPrev = NULL;
                    block = DeRefLink(&globalHeadBlock[stealIndex]);
                    continue;
                }
            }
            else if (block == stealBlock)
            {
                block_t* value = setmark1(getpointer(block));
                block_t* expect = getpointer(block);
                if (CAS(&stealPrev->next, &expect, value))
                {
                    Mark1Block(block);
                    continue;
                }
                else
                {
                    stealPrev = NULL;
                    block = DeRefLink(&globalHeadBlock[stealIndex]);
                    continue;
                }
            }
            else
                stealPrev = block;
        }
        if (block == stealBlock || getpointer(next) == stealBlock)
        {
            block = getpointer(next);
            break;
        }
        block = getpointer(next);
    }
    return block;
}

void *TryStealBlock(int round)
{
    int head = stealHead;
    block_t *block = stealBlock;
    foundAdd = false;
    if (block == NULL)
    {
        block = DeRefLink(&globalHeadBlock[stealIndex]);
        stealBlock = block;
        stealHead = head = 0;
    }
    if (head == MAX_BLOCK_SIZE)
    {
        stealBlock = block = NextStealBlock(block);
        head = 0;
    }
    if (block == NULL)
    {
        stealIndex = (stealIndex + 1) % MAX_NR_THREADS;
        stealHead = 0;
        stealBlock = NULL;
        stealPrev = NULL;
        return NULL;
    }
    if (round == 1)
        NotifyStart(block, threadID);
    else if (round > 1 && NotifyCheck(block, threadID))
        foundAdd = true;
    for (;;)
    {
        if (head >= MAX_BLOCK_SIZE)
        {
            stealHead = head;
            return NULL;
        }
        else
        {
            void *data = block->nodes[head];
            if (data == NULL)
                head++;
            else if (CAS(&block->nodes[head], &data, NULL))
            {
                stealHead = head;
                return data;
            }
        }
    }
}

void *TryRemoveAny()
{
    int head = threadHead - 1;
    block_t *block = threadBlock;
    int round = 0;
    for (;;)
    {
        if (block == NULL || (head < 0 && getpointer(DeRefLink(&block->next)) == NULL))
        {
            do
            {
                int i = 0;
                do
                {
                    void *result = TryStealBlock(round);
                    if (result != NULL)
                        return result;
                    if (foundAdd)
                    {
                        round = 0;
                        i = 0;
                    }
                    else if (stealBlock == NULL)
                        i++;
                } while (i < MAX_NR_THREADS);
            } while (++round <= MAX_NR_THREADS);
            return NULL;
        }
        if (head < 0)
        {
            Mark1Block(block);
            for (;;)
            {
                block_t* next = DeRefLink(&block->next);
                if (ismarked2(next))
                    Mark1Block(next); //Dropping flag 2???
                if (ismarked1(next))
                {
                    if (getpointer(next)!=NULL)
                        NotifyAll(next); //Dropping flags ???
                    if (CAS(&globalHeadBlock[threadID],
                            &block, getpointer(next)))
                    {
                        block->next = (block_t*)setmark1(NULL);
                        DeleteNode(block);
                        ReScan(next);
                        block = next;
                    }
                    else
                        block = DeRefLink(&globalHeadBlock
                                              [threadID]);
                }
                else
                    break;
            }
            threadBlock = block;
            threadHead = MAX_BLOCK_SIZE;
            head = MAX_BLOCK_SIZE - 1;
        }
        else
        {
            DT *data = block->nodes[head];
            if (data == NULL)
                head--;
            else if (CAS(&block->nodes[head], &data, NULL))
            {
                threadHead = head;
                return data;
            }
        }
    }
}

//-------------Memory Management-----------------

block_t* _Atomic NewNode(void)
{
    block_t* _Atomic new = (block_t* _Atomic)malloc(sizeof(block_t));
    block_t* work = new;
    work->next = (block_t* _Atomic)NULL;
    assert(new!=NULL);
    return new;
};

void DeleteNode(block_t *node)
{
    // TODO ?
};

block_t *DeRefLink(struct block_t * _Atomic* link) { 
    if (link == NULL) return NULL;
    return (block_t *)LOAD(link); }

// void ReleaseRef(block_t *node);

void ReScan(block_t* _Atomic node) {
    if (node == NULL) return;
    LOAD(&node);
};

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char * argv[]) {
    double tic, toc;
    int threads;
    if (argc == 2)
    {
        threads = (int)strtol(argv[1], NULL, 10);
    } else threads = 10;

    printf("Running with %d threads\r\n", threads);

    
    InitBag(threads);

    omp_set_num_threads(threads);

    tic = omp_get_wtime();

    /*
    #pragma omp parallel for
    for (int i = 0; i < omp_get_num_threads(); i++)
    {
        int id = omp_get_thread_num();
        InitThread(id);
    }
    for (int i = 0; i < omp_get_num_threads(); i++)
    {
        int id = omp_get_thread_num();
        if (id == 0)
        {
            int *item = malloc(sizeof(int));
            *item = id;
            Add(item);
        } else
        {
            int *result = (int*)TryRemoveAny();
            if (result == NULL)
            {
                 printf("Bag was empty\r\n");
            } else
            printf("Received %d\r\n", *result);
        }
        
        
    }
    */

    long genresult = 0;
    int multiply = 10;

    // #pragma omp parallel for private(thread) shared(threads)
    // for (int i = 0; i < threads; i++)
    // {
    //     int id = omp_get_thread_num();        
    //     thread = InitThread(id);
    //     threadar[id] = thread;        
    //     printf("Hello from thread %d\r\n", id + 1);
    // }
    
    
    #pragma omp parallel for reduction(+:genresult) shared(globalHeadBlock)
    for (int i = 0; i < omp_get_num_threads(); i++)
    {
        int id = omp_get_thread_num();
        //thread = threadar[id];
        InitThread(id);
        printf("Hello from thread %d\r\n", id + 1);
        int mult = multiply;
        long result = 0;
        int *inc = NULL;
        
        if (i < threads/2)
        {
        for (int j = 0; j < mult*(id+1); j++)
        {
            int *item = malloc(sizeof(int));
            *item = id + 1;
            Add(item);
        }
        } else {
        
        do
        {
            inc = (int*)(TryRemoveAny());
            if (inc != (int*)NULL) {
                result += (long)*inc;
                genresult += (long)*inc;
            }
        } while (inc != NULL);
        printf("Thread %d got result %ld\r\n", id + 1, result);
        }
                
    }
    long expected = 0;
    for (int i = 0; i < threads/2; i++)
    {
        expected += multiply*(i+1)*(i+1);
    }
    
    printf("Got %ld overall, from %ld possible\r\n", genresult, expected);
    
        
    toc = omp_get_wtime();

    printf("Unit test took %lf seconds \r\n", toc - tic);
} 