#include "concurrentBags.h"
#include "memoryManagement.h"
#include "config.h"

#include <stdatomic.h> // gcc -latomic
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

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

// Initialization variables
int Nr_threads;
// Shared variables
block_t ** globalHeadBlock;
// Thread-local storage
block_t *threadBlock, *stealBlock, *stealPrev;
bool foundAdd;
int threadHead, stealHead, stealIndex;
int threadID; // Unique number between 0 ... Nr_threads

struct block_t
{
    _Atomic (DT *) nodes[MAX_BLOCK_SIZE]; // changed void*
    _Atomic (long) notifyAdd[MAX_NR_THREADS / WORD_SIZE];
    /* Attention! Also holds marked1 and marked2 in its lsb
    Therefore have to mask when actually dereferencing the pointer
    */
    _Atomic (block_t *) next;
};

void Mark1Block(block_t *block)
{
    for (;;)
    {
        block_t *next = {block->next};
        block_t *update = {next};
        update = setmark1(update);
        //mark2 should have been copied

        if (getpointer(next) == NULL ||
            ismarked1(next) ||
            CAS(&block->next, &next, update))
            break;
    }
}

void NotifyAll(block_t *block)
{
    for (int i = 0; i < (int)(Nr_threads / WORD_SIZE); i++)
        block->notifyAdd[i] = 0;
}

block_t *NewBlock()
{
    block_t *block = (block_t *)NewNode(sizeof(block_t));
    block->next = NULL;
    NotifyAll(block);
    for (int i = 0; i < MAX_BLOCK_SIZE; i++)
        block->nodes[i] = NULL;
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
    Nr_threads = num_threads;
    block_t* hb[Nr_threads];
    globalHeadBlock = hb;

    for (int i = 0; i < Nr_threads; i++)
        globalHeadBlock[i] = (block_t *){0};
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
            block->next = oldblock;
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

block_t *NextStealBlock(block_t *cblock)
{
    block_t *block = {cblock};
    block_t *next;
    for (;;)
    {
        if (block == NULL)
        {
            block = DeRefLink(&globalHeadBlock[stealIndex]);
            break;
        }
        next = DeRefLink(&block->next);
        if (ismarked2(next))
            Mark1Block(next);
        if (stealPrev == NULL || getpointer(next) == NULL)
        {
            if (ismarked1(next))
            {
                if (getpointer(next))
                    NotifyAll(getpointer(next));
                if (CAS(&globalHeadBlock[stealIndex], &block, getpointer(next)))
                {
                    block->next = NULL;
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
                block_t *prevnext = {getpointer(block)};
                if (ismarked2(stealPrev->next)) prevnext = setmark2(prevnext);

                if (CAS(&stealPrev->next, &prevnext, getpointer(next)))
                {
                    block->next = NULL;
                    block->next = setmark1(block->next);
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
                block_t* value = getpointer(block);
                value = setmark2(value);

                if (CAS(&stealPrev->next, &block, value))
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
        stealIndex = (stealIndex + 1) % Nr_threads;
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
        if (head == MAX_BLOCK_SIZE)
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
        if (block == NULL || (head < 0 && getpointer(block->next) ==
                                              NULL))
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
                } while (i < Nr_threads);
            } while (++round <= Nr_threads);
            return NULL;
        }
        if (head < 0)
        {
            Mark1Block(block);
            for (;;)
            {
                block_t *next = {DeRefLink(&block->next)};
                if (ismarked2(next))
                    Mark1Block(next);
                if (ismarked1(next))
                {
                    if (getpointer(next))
                        NotifyAll(getpointer(next));
                    if (CAS(&globalHeadBlock[threadID],
                            &block, getpointer(next)))
                    {
                        block->next = NULL;
                        block->next = setmark1(block->next);
                        DeleteNode(block);
                        ReScan(next);
                        block = getpointer(next);
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
block_t *NewNode(int size)
{
    return malloc(size);
};

void DeleteNode(block_t *node)
{
    // TODO ?
};

block_t *DeRefLink(struct block_t ** link) { return (block_t *)*link; }

// void ReleaseRef(block_t *node);

void ReScan(block_t *node) {
    //TODO
};