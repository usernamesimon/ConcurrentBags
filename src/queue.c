#include "config.h"

#include <stdatomic.h> // gcc -latomic
#include <stdlib.h>
#include <stdio.h>
#include <omp.h>

omp_lock_t enq_lock;
omp_lock_t deq_lock;

struct simple_node {
    DT* val;
    struct simple_node* next;    
};

struct simple_node* tail;
struct simple_node* head;

void enq(DT* x){
    omp_set_lock(&enq_lock);
    struct simple_node* e = (struct simple_node *) malloc(sizeof(struct simple_node));
    e->val = x;
    e->next = NULL;
    tail->next = e;
    tail = e;
    omp_unset_lock(&enq_lock);
}

void init_queue(){
    tail = (struct simple_node *) malloc(sizeof(struct simple_node));
    head = tail;
    tail->next = NULL;
}

void* deq(){
    DT* result;
    omp_set_lock(&deq_lock);
    if (head->next == NULL)
    {
        omp_unset_lock(&deq_lock);
        return NULL;
    }
    result = head->next->val;
    head = head->next;
    omp_unset_lock(&deq_lock);
    return result;
}

int main(int argc, char * argv[]){
    int Nr_threads = 3;
    if (argc == 2)
    {
        Nr_threads = (int)strtol(argv[1], NULL, 10);
    } else Nr_threads = 10;
    
    printf("Running with %d threads\n",Nr_threads);
    omp_set_num_threads(Nr_threads);
    init_queue();
    printf("Initialized Queue\n");
    srand(1);
    printf("Running loop\n");
    #pragma omp parallel for
    for (int i = 0; i < 1000; i++){
        if ((float) rand() / (float)(RAND_MAX) < 0.5 ){printf("Thread %d adding\n",omp_get_thread_num()); enq(&i);}
        else {
        printf("Thread %d removing\n",omp_get_thread_num());
          int *res = (int *)(deq());
        }
    }
}