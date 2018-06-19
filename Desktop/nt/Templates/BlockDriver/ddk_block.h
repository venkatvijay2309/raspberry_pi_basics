#ifndef DDK_BLOCK_H
#define DDK_BLOCK_H

#ifdef __KERNEL__
int block_register_dev(void);
void block_deregister_dev(void);
#endif

#endif
