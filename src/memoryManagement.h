block_t* _Atomic NewNode(void);

void DeleteNode(block_t *node);

block_t * DeRefLink(struct block_t * _Atomic* link);

void ReleaseRef(block_t *node);

void ReScan(block_t* _Atomic node);