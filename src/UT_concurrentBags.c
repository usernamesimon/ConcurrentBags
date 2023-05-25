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
    {
        #pragma omp parallel for
        for (int i=0; i<threads; i++) {
            InitThread(omp_get_thread_num());
        }

        #pragma omp barrier //necessary?

        #pragma omp parallel for
        for (int i = 0; i < threads; i++)
        {
            int val = omp_get_thread_num();
            for (int j = 0; j < i; j++)
            {
                Add(&val);
            }
            for (int j = 0; j < i; j++)
            {
                int *result = (int*)(TryRemoveAny());
                if (!result == NULL) {
                printf("Thread %d trying to read from %x\r\n", val, result);
                printf("Thread %d read value %d\r\n", val, *result);
                } else 
                printf("Thread %d got a NULL Pointer", val);
            }
            
        }
        
    }
    toc = omp_get_wtime();

    printf("Unit test took %lf seconds \r\n", toc - tic);
    

}