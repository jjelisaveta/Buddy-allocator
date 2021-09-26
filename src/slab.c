#define _CRT_SECURE_NO_WARNINGS

#include"slab.h"
#include "buddy.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

#define NAME_LENGTH (20)
#define NUMBER_OF_BUFFERS (13)


HANDLE mutex;

typedef enum shrink_flag { INITIAL, EXPANDED, REDUCED } sflag;
typedef enum error_flag{NO_ERROR_CACHE, NOT_ENOUGH_MEMORY, PARAM_ERROR, CACHE_ALREADY_EXISTS} eflag;

typedef struct kmem_cache_s {
	char name[NAME_LENGTH];
	int num_blocks;  //broj blokova u slabu
	int num_slabs;
	int max_objects;
	int num_objects;
	int disp;  //trenutni pomeraj
	int max_disp;   //sledeci pomeraj
	sflag s_flag;
	eflag e_flag;
	int size;
	double percentage;
	struct kmem_cache_s *next;
	struct kmem_cache_s *prev;
	struct slab* full;
	struct slab* empty;
	struct slab* semi;
	void(*ctor)(void *);  //konstruktor
	void(*dtor)(void *);  //destruktor
} kmem_cache_t;


typedef struct cacheInfo {
	kmem_cache_t *first;
	kmem_cache_t *last;
	kmem_cache_t buffers[NUMBER_OF_BUFFERS];
	int number;
} CacheInfo;

CacheInfo *cache_info;

typedef struct slab {
	void* start;
	void* objects;
	struct slab* next;
} Slab;

void kmem_init(void *space, int block_num) {
	buddy_init(space, block_num); //podaci za buddy se cuvaju u prvom bloku
	printf("start je %lx\n", (char*)space + 8 * BLOCK_SIZE);
	start = (char*)space + BLOCK_SIZE;
	slab_info = start;
	cache_info = (CacheInfo*)start;
	cache_info->first = cache_info->last = NULL;	
	cache_info->number = 0;

	/*initialization of small buffer caches*/
	memset(&cache_info->buffers[0], '\0', NUMBER_OF_BUFFERS * sizeof(kmem_cache_t));
	for (int i = 0; i < NUMBER_OF_BUFFERS; i++) {
		cache_info->buffers[i].next = NULL;
	}
	//start = (char*)start + sizeof(CacheInfo);

	/*start of memory*/
	start = (char*)slab_info + 7 * BLOCK_SIZE;  
	printf("start je %lx\n", (char*)start);
	mutex = CreateMutex(NULL, FALSE, NULL);
}

kmem_cache_t *kmem_cache_create(const char *name, size_t size, void(*ctor)(void *), void(*dtor)(void *)) { // Allocate cache
	WaitForSingleObject(mutex, INFINITE);

	if (size <= 0) {
		printf("Error: size<=0.\n");
		ReleaseMutex(mutex);
		return NULL;
	}
	kmem_cache_t* cur_cache = cache_info->first;
	while (cur_cache) {
		if (strcmp(cur_cache->name, name) == 0) {
			printf("Cache with given name already exists.\n");
			cur_cache->e_flag = CACHE_ALREADY_EXISTS;
			ReleaseMutex(mutex);
			return cur_cache;
		}
		cur_cache = cur_cache->next;
	}
	if (cache_info->first == NULL) {
		cache_info->first = (char*)cache_info + sizeof(CacheInfo);
	}
	kmem_cache_t* new_cache = (kmem_cache_t*)((char*)cache_info->first + cache_info->number * sizeof(kmem_cache_t));
	strcpy(new_cache->name, name);

	if ((size + 1 + sizeof(Slab)) < BLOCK_SIZE / 2)
		new_cache->num_blocks = 4;
	else
		new_cache->num_blocks = (size + 1 + sizeof(Slab)) / BLOCK_SIZE + ((size + 1 + sizeof(Slab)) % BLOCK_SIZE) != 0;

	

	new_cache->max_objects = (new_cache->num_blocks*BLOCK_SIZE - sizeof(Slab)) / (size + 1);   //prvi BAJT za flag da li je zauzeto
	
	new_cache->num_slabs = 0;
	new_cache->num_objects = 0;    
	new_cache->size = size;
	new_cache->percentage = 0;
	new_cache->empty = NULL;
	new_cache->full = NULL;
	new_cache->semi = NULL;
	new_cache->ctor = ctor;
	new_cache->dtor = dtor; 
	new_cache->next = new_cache->prev = NULL;
	new_cache->e_flag = NO_ERROR_CACHE;
	cache_info->number++;

	new_cache->max_disp = new_cache->num_blocks * BLOCK_SIZE - new_cache->max_objects * (new_cache->size + 1) - sizeof(Slab);
	new_cache->disp = 0;
	new_cache->s_flag = INITIAL;  //smisli za shrink
	
	if (cache_info->last) {
		new_cache->prev = cache_info->last;
		cache_info->last->next = new_cache;
	}
	else {
		cache_info->first = new_cache;
		cache_info->first->prev = cache_info->first->next = NULL;
 	}
	cache_info->last = new_cache;
	ReleaseMutex(mutex);
	return new_cache;
}

int kmem_cache_shrink(kmem_cache_t *cachep) { // Shrink cache
	WaitForSingleObject(mutex, INFINITE);

	if (cachep == NULL) {
		cachep->e_flag = PARAM_ERROR;
		ReleaseMutex(mutex);
	}

	if (cachep->s_flag == EXPANDED) {
		ReleaseMutex(mutex);
		return 0;
	}
	int cnt = 0;
	Slab* curr = cachep->empty;
	while (curr) {
		Slab* old = curr;
		curr = curr->next;
		buddy_mem_free(old->start, cachep->num_blocks);
		cnt++;
		cachep->num_slabs--;
	}
	cachep->empty = NULL;
	cachep->s_flag = REDUCED;

	ReleaseMutex(mutex);
	return cnt;
}

Slab* get_slab(kmem_cache_t* cachep) {
	if (cachep->semi == NULL && cachep->empty == NULL) {  //trazi novi

		if (cachep->s_flag == REDUCED)
			cachep->s_flag = EXPANDED;

		Slab* new_slab = (Slab*)buddy_mem_alloc(cachep->num_blocks);
		if (new_slab == NULL) {
			printf("kmem_cache_alloc: Not enough memory for allocating object (%s)", cachep->name);
			cachep->e_flag = NOT_ENOUGH_MEMORY;
			return NULL;
		}

		new_slab = (char*)new_slab + cachep->num_blocks*BLOCK_SIZE - sizeof(Slab);
		new_slab->start = (char*)new_slab - cachep->num_blocks*BLOCK_SIZE + sizeof(Slab);  

		//int tmp = (cachep->num_blocks * BLOCK_SIZE - cachep->max_objects * (cachep->size + 1) - sizeof(Slab)) / CACHE_L1_LINE_SIZE;
		
		if (cachep->disp + CACHE_L1_LINE_SIZE > cachep->max_disp)
			cachep->disp = 0;
		else
			cachep->disp = cachep->disp + CACHE_L1_LINE_SIZE;

		new_slab->objects = (char*)new_slab->start + cachep->disp;
		new_slab->next = NULL;
		memset((char*)new_slab->start, '\0', (cachep->num_blocks*BLOCK_SIZE - sizeof(Slab) - 1));  //stavlja sve na 0
		
		if (cachep->ctor) {  //inicijalizuje sve objekte
			char* curr = new_slab->objects;
			for (int i = 0; i < cachep->max_objects; i++) {
				curr = (char*)curr + 1;
				//if (cachep->ctor) cachep->ctor(curr);
				curr = (char*)curr + cachep->size;
			}
		}
		cachep->semi = new_slab;
		cachep->num_slabs++;
	}
	else if (cachep->semi == NULL && cachep->empty != NULL) {
		cachep->semi = cachep->empty;
		memset(cachep->semi, '\0', (cachep->num_blocks*BLOCK_SIZE- sizeof(Slab)));  //dal valja
		cachep->empty = cachep->empty->next;
		cachep->semi->next = NULL;
	}
	return cachep->semi;
}

void *kmem_cache_alloc(kmem_cache_t *cachep) { // Allocate one object from cache
	WaitForSingleObject(mutex, INFINITE);

	if (cachep == NULL) {
		printf("Error: cache is not created.\n");
		cachep->e_flag = PARAM_ERROR;
		ReleaseMutex(mutex);
		return NULL;
	}
	//postoji semi slab, dodati u njega i proveriti da li je popunjen
	Slab* cur_slab = get_slab(cachep);
	if (cur_slab == NULL) {
		printf("Not enough memory.\n");
		ReleaseMutex(mutex);
		return NULL;
	}
	void *ret = (char*)cur_slab->objects;
	char is_full = *(char*)cur_slab->objects;
	while (is_full) {
		ret = (char*)ret + (cachep->size+1);
		is_full = *(char*)ret;
	}
	//nadjena slobodna
	*(char*)ret = 'f';
	ret = (char*)ret + 1;

	//ako je ova verzija, ne mora sad ctor
	if (cachep->ctor) cachep->ctor(ret);
	cachep->num_objects++;

	if (cachep->num_objects / cachep->num_slabs == cachep->max_objects) {  //popunjen slab
		cachep->semi = cachep->semi->next;  //sigurno jer get_slab vraca pok na semi
		cur_slab->next = cachep->full;
		cachep->full = cur_slab;
	}

	ReleaseMutex(mutex);
	return ret;
}

void kmem_cache_free(kmem_cache_t *cachep, void *objp) { 
	WaitForSingleObject(mutex, INFINITE);

	if (cachep == NULL) {
		printf("Error: Cache does not exist.\n");
		cachep->e_flag = PARAM_ERROR;
		ReleaseMutex(mutex);
		return;
	}
	if (objp == NULL) {
		printf("Error: Object does not exist.\n");
		cachep->e_flag = PARAM_ERROR;
		ReleaseMutex(mutex);
		return;
	}

	//prvo se proverava da li je iz nekog popunjenog slaba
	Slab* prev = NULL;
	Slab* cur_slab = cachep->full;
	while (cur_slab) {
		if ((char*)objp >= (char*)cur_slab->objects &&
			(char*)objp < ((char*)cur_slab/*->objects + cachep->max_objects*cachep->size*/)) {  //nalazi se u ovom slabu
			cachep->num_objects--;

			if (cachep->dtor) cachep->dtor(objp);
			//if (cachep->ctor) cachep->ctor(objp);

			objp = (char*)objp - 1;
			memset(objp, '\0', 1);

			if (prev)
				prev->next = cur_slab->next;
			else
				cachep->full = cur_slab->next;
			cur_slab->next = cachep->semi;
			cachep->semi = cur_slab;
		}
		prev = cur_slab;
		cur_slab = cur_slab->next;
	}
	prev = NULL;
	cur_slab = cachep->semi;
	while (cur_slab) {
		if ((char*)objp >= (char*)cur_slab->objects &&
			(char*)objp < ((char*)cur_slab/*->objects + cachep->max_objects*cachep->size*/)) {  //nalazi se u ovo slabu 
			cachep->num_objects--;

			if (cachep->dtor)cachep->dtor(objp);
			//if (cachep->ctor) cachep->ctor(objp);

			objp = (char*)objp - 1;
			memset(objp, '\0', 1);

			//proveri je li ceo prazan
			void* check = cur_slab->objects;
			for (int j = 0; j < cachep->max_objects; j++) {
				char full = *(char*)check;
				if (full != '\0') {
					ReleaseMutex(mutex);
					return;
				}
				check = (char*)check + (cachep->size + 1);
			}
			if (prev)
				prev->next = cur_slab->next;
			else
				cachep->semi = cur_slab->next;
			cur_slab->next = cachep->empty;
			cachep->empty = cur_slab;
		}
		prev = cur_slab;
		cur_slab = cur_slab->next;
	}
	ReleaseMutex(mutex);
}

void *kmalloc(size_t size) { // Allocate one small memory buffer
	WaitForSingleObject(mutex, INFINITE);

	int ind = round_pow2(size + sizeof(Slab));
	kmem_cache_t* cur_cache = &(cache_info->buffers[ind - 5]);

	if (strcmp((char*)cur_cache->name, "")==0) {  //nije inicijalizovan kes
		strcpy(cur_cache->name, "name-");
		char str[15];
		_itoa(ind, str, 10);
		strcat(cur_cache->name, str);
		strcat(cur_cache->name, "\0");

		if ((size + 1 + sizeof(Slab)) < BLOCK_SIZE / 2)
			cur_cache->num_blocks = 2;
		else 
			cur_cache->num_blocks = (size + 1 + sizeof(Slab)) / BLOCK_SIZE + (((size + 1 + sizeof(Slab)) % BLOCK_SIZE) != 0);
		cur_cache->max_objects = (cur_cache->num_blocks*BLOCK_SIZE-sizeof(Slab)) / (size + 1);

		cur_cache->num_slabs = 0;
		cur_cache->num_objects = 0;    
		cur_cache->size = size;
		cur_cache->percentage = 0;
		cur_cache->empty = NULL;
		cur_cache->full = NULL;
		cur_cache->semi = NULL;
		cur_cache->ctor = cur_cache->dtor = NULL;  //ne postoje 
		cur_cache->next = cur_cache->prev = NULL;
		cur_cache->s_flag = INITIAL;
		cur_cache->max_disp = cur_cache->disp = 0;
		cur_cache->e_flag = NO_ERROR_CACHE;
	}

	Slab* cur_slab = get_slab(cur_cache);
	if (cur_slab == NULL) {
		ReleaseMutex(mutex);
		return NULL;
	}
	void *ret = (char*)cur_slab->objects;
	char is_full = *(char*)cur_slab->objects;
	while (is_full) {
		ret = (char*)ret + (size + 1);
		is_full = *((char*)ret);
	}
	*(char*)ret = 'f';
	ret = (char*)ret + 1;
	cur_cache->num_objects++;
	if (cur_cache->num_objects / cur_cache->num_slabs == cur_cache->max_objects) {  //popunjen slab
		cur_cache->semi = cur_cache->semi->next;
		cur_slab->next = cur_cache->full;
		cur_cache->full = cur_slab;
	}
	//printf("kmalloc\n");
	//printMem();
	ReleaseMutex(mutex);

	
	return ret;
}

void kfree(const void *objp) {
	WaitForSingleObject(mutex, INFINITE);
	if (objp == NULL) {
		ReleaseMutex(mutex);
		return;
	}
	for (int i = 0; i < NUMBER_OF_BUFFERS; i++) {
		if (strcmp((char*)&cache_info->buffers[i],"\0")!=0 && cache_info->buffers[i].num_objects!=0) {   //OVO DELUJE KAO FAIL
			Slab* prev = NULL;
			Slab* cur_slab = cache_info->buffers[i].full;
			while (cur_slab) {
				if ((char*)objp >= (char*)cur_slab->objects && 
					(char*)objp < ((char*)cur_slab/*->objects + cache_info->buffers[i].max_objects*cache_info->buffers[i].size*/)) {  //nalazi se u ovom slabu
					cache_info->buffers[i].num_objects--;
					objp = (char*)objp - 1;
					memset(objp, '\0', cache_info->buffers[i].size+1);

					//prevezi u semi
					if (prev)
						prev->next = cur_slab->next;
					else
						cache_info->buffers[i].full = cur_slab->next;
					cur_slab->next = cache_info->buffers[i].semi;
					cache_info->buffers[i].semi = cur_slab;
				}
				prev = cur_slab;
				cur_slab = cur_slab->next;
			}
			prev = NULL;
			cur_slab = cache_info->buffers[i].semi;
			while (cur_slab) {
				if ((char*)objp >= (char*)cur_slab->objects &&
					(char*)objp < ((char*)cur_slab/*->objects + cache_info->buffers[i].max_objects*cache_info->buffers[i].size*/)) {  //nalazi se u ovo slabu 
					cache_info->buffers[i].num_objects--;
					objp = (char*)objp - 1;  //vrati tamo gde treba da se oznaci flag
					memset(objp, '\0', cache_info->buffers[i].size + 1);  //gluperdo

					//proveri jel ceo prazan
					void* check = (char*)cur_slab->objects;
					for (int j = 0; j < cache_info->buffers[i].max_objects; j++) {
						char full = *(char*)check;
						if (full != '\0') {
							ReleaseMutex(mutex);
							return;
						}
						check = (char*)check + cache_info->buffers[i].size + 1;
					}
					if (prev)
						prev->next = cur_slab->next;
					else
						cache_info->buffers[i].semi = cur_slab->next;
					cur_slab->next = cache_info->buffers[i].empty;
					cache_info->buffers[i].empty = cur_slab;

					/*
					buddy_mem_free(cur_slab->start, cache_info->buffers[i].num_blocks);
					*/

				}
				prev = cur_slab;
				cur_slab = cur_slab->next;
			}
		}
	}

	ReleaseMutex(mutex);
}



void kmem_cache_destroy(kmem_cache_t *cachep) { // Deallocate cache
	WaitForSingleObject(mutex, INFINITE);

	if (cachep == NULL) {
		printf("Error: Cache does not exist.\n");
		ReleaseMutex(mutex);
		return;
	}

	Slab* cur_cache = cachep->full;
	while (cur_cache) {
		Slab *old = cur_cache;
		cur_cache = cur_cache->next;
		buddy_mem_free(old->start, cachep->num_blocks);
	}
	cur_cache = cachep->semi;
	while (cur_cache) {
		Slab *old = cur_cache;
		cur_cache = cur_cache->next;
		buddy_mem_free(old->start, cachep->num_blocks);
	}
	cur_cache = cachep->empty;
	while (cur_cache) {
		Slab *old = cur_cache;
		cur_cache = cur_cache->next;
		buddy_mem_free(old->start, cachep->num_blocks);
	}
	if (cachep->prev)
		cachep->prev->next = cachep->next;
	else
		cache_info->first = cachep->next;
	if (cachep->next)
		cachep->next->prev = cachep->prev;
	else
		cache_info->last = cachep->prev;

	ReleaseMutex(mutex);
}


void kmem_cache_info(kmem_cache_t *cachep) { // Print cache info

	WaitForSingleObject(mutex, INFINITE);

	if (cachep == NULL) {
		printf("Error: Parameter cachep is null pointer.\n");
		ReleaseMutex(mutex);
		return;
	}

	printf("Cache %s:\n", cachep->name);
	printf("   -object size: %dB\n", cachep->size);
	printf("   -cache size: %d blocks\n", cachep->num_blocks);
	printf("   -number of slabs: %d\n", cachep->num_slabs);
	printf("   -number of objects in one slab: %d\n",cachep->max_objects);
	cachep->percentage = ((double)(/*cachep->size**/cachep->num_objects)) / ((double)(cachep->max_objects*/*BLOCK_SIZE **/ cachep->num_slabs)) * 100;
	printf("   -percentage: %0.2f\n", cachep->percentage);

	ReleaseMutex(mutex);
}

int kmem_cache_error(kmem_cache_t *cachep) { // Print error message
	WaitForSingleObject(mutex, INFINITE);

	printf("Error: %s", cachep->e_flag);

	ReleaseMutex(mutex);
	return cachep->e_flag;
}


void printMem() {
	WaitForSingleObject(mutex, INFINITE);
	int sum = 0;
	for (int i = 0; i < 10; i++) {
		printf("lista %d ", i);
		list_info_current = (char*)list_info_first + i * sizeof(ListInfo);
		if (list_info_current->first == 0) {
			printf("0\n");
		}
		else {
			int cnt = 0;
			void *curr = list_info_current->first;
			while (curr) {
				cnt++;
				curr = ((Pointer*)curr)->next;
			}
			sum += cnt * (1UL << i);
			printf("%d\n", cnt);
		}
	}
	printf("ukupno %d\n", sum);
	printf("\n\n\n");
	ReleaseMutex(mutex);
}