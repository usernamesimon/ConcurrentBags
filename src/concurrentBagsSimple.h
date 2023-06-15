typedef struct block_t block_t;
// Thread-local storage

//Has to be called once to initiate structure
void InitBag(int num_threads);
//Has to be called by each thread to initiate thread local variables
void InitThread(int id);


void Add(void *item);
void *TryRemoveAny();

block_t* _Atomic NewNode(int);

void DeleteNode(block_t *node);

block_t * DeRefLink(struct block_t * _Atomic* link);

void ReleaseRef(block_t *node);

void ReScan(block_t* _Atomic node);
