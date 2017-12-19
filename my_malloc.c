/*
 * CS 2110 Fall 2017
 * Author: Naoto Abe
 */

/* we need this for uintptr_t */
#include <stdint.h>
/* we need this for memcpy/memset */
#include <string.h>
/* we need this for my_sbrk */
#include "my_sbrk.h"
/* we need this for the metadata_t struct definition */
#include "my_malloc.h"

/* You *MUST* use this macro when calling my_sbrk to allocate the
 * appropriate size. Failure to do so may result in an incorrect
 * grading!
 */
#define SBRK_SIZE 2048

/* If you want to use debugging printouts, it is HIGHLY recommended
 * to use this macro or something similar. If you produce output from
 * your code then you may receive a 20 point deduction. You have been
 * warned.
 */
#ifdef DEBUG
#define DEBUG_PRINT(x) printf(x)
#else
#define DEBUG_PRINT(x)
#endif

/* Our freelist structure - this is where the current freelist of
 * blocks will be maintained. failure to maintain the list inside
 * of this structure will result in no credit, as the grader will
 * expect it to be maintained here.
 * DO NOT CHANGE the way this structure is declared
 * or it will break the autograder.
 */
metadata_t* freelist;

/* helpher method to create a new metadata*/
metadata_t* meta(void* address, metadata_t* next, 
	unsigned short blocksize, unsigned short requestsize, unsigned int canary)
{
	metadata_t* newMeta = address;
	newMeta->next = next;
	newMeta->block_size = blocksize;
	newMeta->request_size = requestsize;
	newMeta->canary = canary;
	return newMeta;
}

/* a helper method used in my_malloc that handles splitting of a big block, and  
*  returns the useable block. Puts the rest of the block back into the freelist
*/
metadata_t* breakBigBlock(metadata_t** prev, metadata_t** curr, size_t realSize) 
{
	metadata_t* ans;
	if ((*curr)->block_size < realSize + sizeof(metadata_t) + sizeof(int) + 1) {
		if ((*prev) != NULL) {
			(*prev)->next = (*curr)->next;
		}
		if ((*curr) == freelist) {
			freelist = NULL;
		}
		ans = (*curr);
		unsigned short requestsize = realSize - sizeof(metadata_t) - sizeof(int);
		ans->request_size = requestsize;
		ans->block_size = realSize;
		ans->canary =((((int)(*curr)->block_size) << 16) | ((int)(*curr)->request_size))
            ^ (int)(uintptr_t)(*curr);
        *(int*)((char*)ans + ans->block_size - sizeof(int)) = ans->canary;
	} else {
		metadata_t* newMeta = meta((char*)(*curr) + realSize, (*curr)->next, (*curr)->block_size - realSize, 0, 0);
		if ((*prev)!= NULL && (*curr)->next != NULL) {
			(*prev)->next = newMeta;
			newMeta->next = (*curr)->next;
		} else if ((*prev)!= NULL) {
			(*prev)->next = newMeta;
		} else if ((*curr)->next!=NULL) {
			newMeta->next = (*curr)->next;
			freelist = newMeta;
		} else {
			freelist = newMeta;
		}
		ans = (*curr);
		unsigned short requestsize = realSize - sizeof(metadata_t) - sizeof(int);
		ans->request_size = requestsize;
		ans->block_size = realSize;

		ans->canary =((((int)(*curr)->block_size) << 16) | ((int)(*curr)->request_size))
            ^ (int)(uintptr_t)(*curr); 
        *(int*)((char*)ans + ans->block_size - sizeof(int)) = ans->canary;
	}
	return ans;
}

/* Create a new free list when there is not enough memory to allocate, or the old one is empty */
metadata_t* newfreelist() 
{
	void* start = my_sbrk(SBRK_SIZE);

	if (start == NULL) {
		ERRNO = OUT_OF_MEMORY;
		return NULL;
	}
	metadata_t* newHead = meta(start, NULL, SBRK_SIZE, 0, 0);
	ERRNO = NO_ERROR;
	return newHead;
}

/* a method that allocates a new block of memory for the user.*/
void* my_malloc(size_t size) {
	
	/* calculate the actual size of the block including the metadata */
	size_t real_size = sizeof(metadata_t) + size + sizeof(int);

	if (real_size > SBRK_SIZE)
	{
		ERRNO = SINGLE_REQUEST_TOO_LARGE;
		return NULL;
	}

	/* create a new freelist if freelist is empty */
	if (freelist == NULL)
	{
		if ((freelist = newfreelist()) == NULL) {
			return NULL;
		}
	}

	/* find the best fit block in the freelist, starting from the head of the freelist */
	metadata_t* curr = freelist;
	metadata_t* minBlock = NULL;
	metadata_t* secondlast = NULL;
	while (curr != NULL && curr->block_size < real_size) {
		secondlast = curr;
		curr = curr->next;
	}

	/* we could not find the best fit block. all the block in the freelist was not big enough
	   so allocate another 2KB memory and use that block*/
 	if (curr == NULL) {
 		metadata_t* newlist = newfreelist();
 		if (newlist == NULL) {
 			return NULL;
 		}
 		curr = newlist;

 	}

	metadata_t* tmp = breakBigBlock(&secondlast ,&curr, real_size);

	ERRNO = NO_ERROR;
    return (void*)((char*)tmp + sizeof(metadata_t));
}

/* reallocates the memory block pointed to by ptr, and either shrinks the block or 
   expands the block depending on the new_size */
void* my_realloc(void* ptr, size_t new_size) {
    if (ptr == NULL) {
    	return my_malloc(new_size);
    }
    if (new_size == 0) {
    	my_free(ptr);
    	return NULL;
    }
    void* ptr2 = my_malloc(new_size);
    if (ptr2 == NULL) {
    	my_free(ptr);
    	return NULL;
    }
	metadata_t* temp = (metadata_t*)ptr- 1;
    metadata_t* temp2 = (metadata_t*)ptr2 -1;
    if (temp2->request_size > temp->request_size) {
    	memcpy(ptr2, ptr, temp->request_size);
    } else {
    	memcpy(ptr2, ptr, temp2->request_size);
    }
    my_free(ptr);
    ERRNO = NO_ERROR;
    return ptr2;
}

void* my_calloc(size_t nmemb, size_t size) {
	void* ptr = my_malloc(nmemb*size);
	if (ptr == NULL) {
		return NULL;
	}
	memset(ptr, 0, nmemb * size);
	ERRNO = NO_ERROR;
    return ptr;
}

/* a helper methoed that hanldes merging of adjacent blocks into one block */
int merge(metadata_t* curr, metadata_t* left, metadata_t* rightPre, metadata_t* right) {
	if (left != NULL) {
		left->block_size += curr->block_size;
		if (right != NULL) {
			left->block_size += right->block_size;
			left->next = right->next; 
		}
		return 1;
	}
	if (right != NULL) {
		curr->block_size += right->block_size;
		curr->request_size = 0;
		curr->next = right->next;
		if (rightPre != NULL) {
			rightPre->next = curr;
		}
		if (freelist == right) {
			freelist = curr;
		}
		return 1;
	}
	return 0;
}
void my_free(void* ptr) {
	if (ptr == NULL) {
		return;
	}
	/* create pointers of the block you are freeing. start_ptr points to the head,
	 * end_ptr points to the tail, temp casts the block to metadata so
	 * so we can use it in this method 
	 */
	void* start_ptr = (void*)((char*)ptr  - sizeof(metadata_t));
	metadata_t* temp = (metadata_t*)start_ptr;
	void* end_ptr = (void*)((char*)start_ptr + temp->block_size);
	int canary =((((int)temp->block_size) << 16) | ((int)temp->request_size))
            ^ (int)(uintptr_t)temp;

    /* check for corrupted canary */
	if (temp->canary!= canary || canary != *(int*)((char*)temp + temp->block_size - sizeof(int))) {
		ERRNO = CANARY_CORRUPTED;
		return;
	}

	/* empty metadata containers to be filled and used in the merge method (if one exists) */
	 metadata_t* left = NULL;
	 metadata_t* rightPre = NULL;
	 metadata_t* right = NULL;
     

	metadata_t* current = freelist;

	/* itererate through the freelist */
    if (freelist == NULL) {
    	temp->request_size = 0;
    	freelist = temp;
    } else {
	    while(current != NULL) {
	    	if(current->next == end_ptr) {
	    		rightPre = current;
	    	}
	        if(current == end_ptr) {
	        	right = current;
	        }
	        if((void*)((char*)current + current->block_size) == start_ptr) {
	        	left = current;
	        }
	        current = current->next;

	    } 
	    int merged = merge(temp, left, rightPre, right);
	    if (!merged) {
		temp->next = freelist;
		temp->request_size = 0;
		freelist = temp;
		}
	}
	ERRNO = NO_ERROR;
}



