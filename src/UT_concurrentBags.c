#include "concurrentBags.h"
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
    int multiply = 100;
    #pragma omp parallel for reduction(+:genresult) private(threadBlock, stealBlock, stealPrev, foundAdd, threadHead, stealHead, stealIndex, threadID)
    for (int i = 0; i < omp_get_num_threads(); i++)
    {
        int id = omp_get_thread_num();
        int mult = multiply;
        long result = 0;
        InitThread(id);

        printf("Hello from thread %d\r\n", id + 1);
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