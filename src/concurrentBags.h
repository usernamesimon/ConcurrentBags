typedef struct block_t block_t;
typedef struct  TLS_t TLS_t;
// Thread-local storage

//Has to be called once to initiate structure
void InitBag(int num_threads);
//Has to be called by each thread to initiate thread local variables
TLS_t *InitThread(int id);


void Add(TLS_t *thread, void *item);
void *TryRemoveAny(TLS_t *thread);


