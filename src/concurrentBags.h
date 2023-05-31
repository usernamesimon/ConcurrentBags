typedef struct block_t block_t;

//Has to be called once to initiate structure
void InitBag(int num_threads);
//Has to be called by each thread to initiate thread local variables
void InitThread(int id);


void Add(void *item);
void *TryRemoveAny();


