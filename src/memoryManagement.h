block_t * NewNode(int size);

void DeleteNode(block_t *node);

block_t * DeRefLink(struct block_t ** link);

void ReleaseRef(block_t *node);

void ReScan(block_t *node);