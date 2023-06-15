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

struct TLS_t{
    // Thread-local storage
    block_t * threadBlock, * stealBlock, * stealPrev;
    bool foundAdd;
    int threadHead, stealHead, stealIndex;
    int threadID; // Unique number between 0 ... MAX_NR_THREADS
};

struct block_t
{
    DT * nodes[MAX_BLOCK_SIZE]; // changed void*
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
        block_t* next = block->next;
        block_t* new = next;
        new = setmark1(new);

        if (getpointer(next) == NULL ||
            ismarked1(next) ||
            CAS(&block->next, &next, new))
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

TLS_t *InitThread(int id)
{
    TLS_t *thread = (TLS_t*)malloc(sizeof(TLS_t));
    thread->threadID = id;
    thread->threadBlock = globalHeadBlock[thread->threadID];
    thread->threadHead = MAX_BLOCK_SIZE;
    thread->stealIndex = 0;
    thread->stealBlock = (block_t *)NULL;
    thread->stealPrev = (block_t *)NULL;
    thread->stealHead = MAX_BLOCK_SIZE;
    return thread;
}

void Add(TLS_t *thread, void *item)
{
    int head = thread->threadHead;
    block_t *block = thread->threadBlock;
    for (;;)
    {
        if (head == MAX_BLOCK_SIZE)
        {
            block_t *oldblock = block;
            block = NewBlock();
            block_t* new = getpointer(oldblock); //equivalent to setting flags to false
            block->next = new; //? true, true?
            globalHeadBlock[thread->threadID] = block;
            thread->threadBlock = block;
            head = 0;
        }
        else if (block->nodes[head] == NULL)
        {
            NotifyAll(block);
            block->nodes[head] = item;
            thread->threadHead = head + 1;
            return;
        }
        else
            head++;
    }
}

block_t *NextStealBlock(TLS_t *thread, block_t *block)
{
    block_t* next;
    for (;;)
    {
        if (block == NULL)
        {
            block = DeRefLink(&globalHeadBlock[thread->stealIndex]);
            break;
        }
        next = DeRefLink(&block->next);
        if (ismarked2(next)) Mark1Block(next);
        if (thread->stealPrev == NULL || getpointer(next) == NULL)
        {
            if (ismarked1(next))
            {
                if (getpointer(next) != NULL) NotifyAll(getpointer(next));
                if (CAS(&globalHeadBlock[thread->stealIndex], &block, getpointer(next)))
                {
                    block->next = setmark1((block_t*){NULL});
                    DeleteNode(block);
                    ReScan(next);
                }
                else
                {
                    thread->stealPrev = NULL;
                    block = DeRefLink(&globalHeadBlock[thread->stealIndex]);
                    continue;
                }
            }
            else
                thread->stealPrev = block;
        }
        else
        {
            if (ismarked1(next))
            {
                block_t* copy = thread->stealPrev->next;
                block_t* prevnext = (block_t*)getpointer(block);
                if (ismarked2(copy)) prevnext = setmark2(prevnext);
                block_t* new = (block_t*){next};
                
                if (CAS(&thread->stealPrev->next, &prevnext, new))
                {
                    block->next = setmark1(NULL);
                    DeleteNode(block);
                    ReScan(next);
                }
                else
                {
                    thread->stealPrev = NULL;
                    block = DeRefLink(&globalHeadBlock[thread->stealIndex]);
                    continue;
                }
            }
            else if (block == thread->stealBlock)
            {
                block_t* value = setmark2(getpointer(block));
                block_t* expect = getpointer(block);
                if (CAS(&thread->stealPrev->next, &expect, value))
                {
                    Mark1Block(block);
                    continue;
                }
                else
                {
                    thread->stealPrev = NULL;
                    block = DeRefLink(&globalHeadBlock[thread->stealIndex]);
                    continue;
                }
            }
            else
                thread->stealPrev = block;
        }
        if (block == thread->stealBlock || getpointer(next) == thread->stealBlock)
        {
            block = getpointer(next);
            break;
        }
        block = getpointer(next);
    }
    return block;
}

void *TryStealBlock(TLS_t *thread, int round)
{
    int head = thread->stealHead;
    block_t *block = thread->stealBlock;
    thread->foundAdd = false;
    if (block == NULL)
    {
        block = DeRefLink(&globalHeadBlock[thread->stealIndex]);
        thread->stealBlock = block;
        thread->stealHead = head = 0;
    }
    if (head == MAX_BLOCK_SIZE)
    {
        thread->stealBlock = block = NextStealBlock(thread, block);
        head = 0;
    }
    if (block == NULL)
    {
        thread->stealIndex = (thread->stealIndex + 1) % MAX_NR_THREADS;
        thread->stealHead = 0;
        thread->stealBlock = NULL;
        thread->stealPrev = NULL;
        return NULL;
    }
    if (round == 1)
        NotifyStart(block, thread->threadID);
    else if (round > 1 && NotifyCheck(block, thread->threadID))
        thread->foundAdd = true;
    for (;;)
    {
        if (head == MAX_BLOCK_SIZE)
        {
            thread->stealHead = head;
            return NULL;
        }
        else
        {
            void *data = block->nodes[head];
            if (data == NULL)
                head++;
            else if (CAS(&block->nodes[head], &data, NULL))
            {
                thread->stealHead = head;
                return data;
            }
        }
    }
}

void *TryRemoveAny(TLS_t *thread)
{
    int head = thread->threadHead - 1;
    block_t *block = thread->threadBlock;
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
                    void *result = TryStealBlock(thread, round);
                    if (result != NULL)
                        return result;
                    if (thread->foundAdd)
                    {
                        round = 0;
                        i = 0;
                    }
                    else if (thread->stealBlock == NULL)
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
                    Mark1Block(getpointer(next)); //Dropping flag 2???
                if (ismarked1(next))
                {
                    if (getpointer(next)!=NULL)
                        NotifyAll(getpointer(next)); //Dropping flags ???
                    if (CAS(&globalHeadBlock[thread->threadID],
                            &block, getpointer(next)))
                    {
                        block->next = (block_t*)setmark1(NULL);
                        DeleteNode(block);
                        ReScan(next);
                        block = getpointer(next);
                    }
                    else
                        block = DeRefLink(&globalHeadBlock
                                              [thread->threadID]);
                }
                else
                    break;
            }
            thread->threadBlock = block;
            thread->threadHead = MAX_BLOCK_SIZE;
            head = MAX_BLOCK_SIZE - 1;
        }
        else
        {
            DT *data = block->nodes[head];
            if (data == NULL)
                head--;
            else if (CAS(&block->nodes[head], &data, NULL))
            {
                thread->threadHead = head;
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
    int multiply = 500;
    TLS_t* threadbuf[threads];
    TLS_t** threadar = threadbuf;
    TLS_t* thread;

    // #pragma omp parallel for private(thread) shared(threads)
    // for (int i = 0; i < threads; i++)
    // {
    //     int id = omp_get_thread_num();        
    //     thread = InitThread(id);
    //     threadar[id] = thread;        
    //     printf("Hello from thread %d\r\n", id + 1);
    // }
    
    
    #pragma omp parallel for reduction(+:genresult) private(thread) shared(globalHeadBlock)
    for (int i = 0; i < omp_get_num_threads(); i++)
    {
        int id = omp_get_thread_num();
        //thread = threadar[id];
        thread = InitThread(id);
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
            Add(thread, item);
        }
        } else {
        
        do
        {
            inc = (int*)(TryRemoveAny(thread));
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