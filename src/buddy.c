#define _CRT_SECURE_NO_WARNINGS
#include "buddy.h"
#include "slab.h"
#include <stdio.h>
#include <stdlib.h>



void check_asc(ListInfo *list) {
	void* fir = list->first;
	while (fir) {
		void* cur = ((Pointer*)fir)->next;
		if ((char*)fir > (char*)cur && cur!=NULL) {
			printf("nije dobro\n");
			break;
		}
		fir = ((Pointer*)fir)->next;;
	}
}



void buddy_init(void *space, size_t size) {
	start = (char*)space + 1 * BLOCK_SIZE;   //JEL OVO OK

	buddy_info = (BuddyInfo*)space;
	buddy_info->free_size = size - 8;  //prvi blok se koriste za buddy podatke
	size -= 8;
	list_info_first = (char*)buddy_info + sizeof(BuddyInfo);
	buddy_info->first = (ListInfo*)list_info_first;
	buddy_info->last =(ListInfo*)( (char*)list_info_first + 31 * sizeof(ListInfo));
	space = (char*)space + 8 * BLOCK_SIZE;
	for (int i = 31; i >= 0; i--) {
		list_info_current = (char*)list_info_first + i * sizeof(ListInfo);
		if (size >= (1UL << i)) {
			list_info_current->first = list_info_current->last = (char*)space;
			((Pointer*)list_info_current->first)->next = NULL;
			size -= (1UL << i);
			space =(void*) ((char *)space + (1UL << i)*BLOCK_SIZE);
		}
		else {
			list_info_current->first = list_info_current->last = NULL;
		}
	}
	list_info_first = (char*)buddy_info->first;
	start = (char*)start + 7 * BLOCK_SIZE;
}

void* buddy_mem_alloc(size_t size) {
	if (size <= 0 || size > (1UL << 31)) {
		printf("Neispravan parametar size.\n");
		return NULL;
	}
	if (size > buddy_info->free_size) {
		printf("Nema dovoljno memorije.\n");
		return NULL;
	}
	list_info_first = buddy_info->first;
	ListInfo* a = (ListInfo*)((char*)list_info_first + 2 * sizeof(ListInfo));
	current = NULL;
	int ind = round_pow2(size);
	list_info_current = (ListInfo*)((char*)list_info_first + ind * sizeof(ListInfo));
	
	if (list_info_current->first) {  //ima, uzima se prvi i pomeri se first polje
		current = list_info_current->first;
		list_info_current->first = ((Pointer*)list_info_current->first)->next;   //prepisuje adresu zapisanu u current(first) u first
		if (list_info_current->first == NULL) list_info_current->last = NULL;
	}
	else {  //nema, trazi podelu veceg
		int i = ind + 1;
		
		list_info_current = (ListInfo*)((char*)list_info_first + i * sizeof(ListInfo));
		while (list_info_current->first == NULL) {
			i++;
			list_info_current = (ListInfo*)((char*)list_info_first + i * sizeof(ListInfo));
		}
		//pronadjen veci chunk od potrebnog, treba ga deliti
		current = list_info_current->first;
		list_info_current->first = ((Pointer*)list_info_current->first)->next;
		if (list_info_current->first == NULL) list_info_current->last = NULL;
		void *help = NULL;
		//sve povezati
		while (i > ind) {
			i--;
			help = (char*)current + (1 << i)*BLOCK_SIZE;
			list_info_current = (ListInfo*)((char*)list_info_first + i * sizeof(ListInfo));
			insert_sorted_mem(list_info_current, help);
		}
		buddy_info->free_size -= size;
		//spojiti neke delove ako je moguce
		shrink();
	}
	
	return current;
}

void buddy_mem_free(void* ptr, size_t size) {

	if (ptr == NULL) {
		printf("mem_free: Prosledjen je NULL pokazivac.\n");
		return;
	}
	if (size <= 0) {
		printf("mem_free: Neispravna velicina.\n");
		return;
	}
	int ind = round_pow2(size);
	list_info_current = (char*)list_info_first + ind * sizeof(ListInfo);
	insert_sorted_mem(list_info_current, ptr);
	buddy_info->free_size += size;
	shrink();
}

int round_pow2(int n) {
	int i = 0;
	while (n > (1UL << i)) {
		i++;
	}
	return i;
}


void shrink() {
	for (int i = 0; i < 32; i++) {
		list_info_current = (char*)list_info_first + i * sizeof(ListInfo);
		check_asc(list_info_current);
		void *prev = NULL;
		void *fir = list_info_current->first;  //prvi u listi
		void *sec = NULL;     //sledeci
		if (fir) sec = ((Pointer*)fir)->next;
		else list_info_current->last = NULL;
		while (fir && sec) {
			long is_buddy = ((char*)fir - (char*)start)/BLOCK_SIZE;
			is_buddy = is_buddy % (1UL << (i + 1));
			if (((char*)fir + (1UL<<i) * BLOCK_SIZE == (char*)sec) && is_buddy==0){
				
				//izbaci iz liste i umetni u vecu
				if (prev) {
					((Pointer*)prev)->next = ((Pointer*)sec)->next;
				}
				else {
					((Pointer*)list_info_current)->next = ((Pointer*)sec)->next;
				}
				ListInfo* next_list = (char*)list_info_first + (i+1) * sizeof(ListInfo);
				insert_sorted_mem(next_list, fir);
				fir = ((Pointer*)sec)->next;
				if (fir) sec = ((Pointer*)fir)->next;
				else sec = NULL;
			}
			else {
				prev = fir;
				fir = sec;
				sec = ((Pointer*)sec)->next;
			}
		}
	}

}

void insert_sorted_mem(ListInfo* list, void *cur) {
	void *prev = NULL;
	void *fir = list->first;

	while (fir && ((char*)fir < (char*)cur)) {    //umetanje u rastuce sortiranu listu
		prev = fir;
		fir = ((Pointer*)fir)->next;   //tmp postaje ono sto je zapisano na adresi na koju ukazuje
	}
	if (prev) {
		((Pointer*)prev)->next = cur;
		((Pointer*)cur)->next = fir;
	}
	else {
		((Pointer*)cur)->next = list->first;
		list->first = cur;
	}
}

/*
int main() {
	void* space = malloc(264 * BLOCK_SIZE);
	buddy_init(space, 264);

	void **ptr = malloc(10 * sizeof(void*));
	for (int i = 1; i <= 10; i++) {
		ptr[i - 1] = buddy_mem_alloc(i);
	}

	for (int i = 1; i < 11; i++) {
		
		buddy_mem_free(ptr[i - 1], i);
	}

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
				printf("curr = %lx, za spajanje treba %lx\n", (char*)curr, (char*)((char*)curr + (1UL << i)));
				curr = ((Pointer*)curr)->next;
			}
			printf("%d\n", cnt);
		}
	}
	


	system("pause");
}*/