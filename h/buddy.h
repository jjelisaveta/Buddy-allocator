#ifndef _BUDDY_
#define _BUDDY_

#define BLOCK_SIZE (4096)
#define CACHE_L1_LINE_SIZE (64)



typedef struct buddyInfo {
	struct listInfo *first;
	struct listInfo *last;
	int free_size;
} BuddyInfo;

typedef struct pointer {
	void* next;
} Pointer;

typedef struct listInfo {
	void* first;
	void* last;
} ListInfo;


void buddy_init(void *space, size_t size);
void* buddy_mem_alloc(size_t size);

void buddy_mem_free(void* ptr, size_t size);
void shrink();
void insert_sorted_mem(ListInfo* list, void *cur);
int round_pow2(int n);

BuddyInfo *buddy_info;
ListInfo *list_info_first;
ListInfo *list_info_current;
void* start;
void* current;
void* slab_info;


#endif