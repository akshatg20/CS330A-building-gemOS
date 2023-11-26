// #include <stdio.h>
// #include <stdlib.h>
// #include <unistd.h>
// #include <sys/mman.h>

// // defining macros for the metadata size for allocated and free memory chunks
// #define ALLOC_MEM_METADATA 8
// #define FREE_MEM_METADATA 24

// unsigned long FOUR_MB = 4*1024*1024;

// // defining a structure to store the free memory chunk
// typedef struct FreeChunk {
// 	unsigned long Size;			// size of the chunk
// 	struct FreeChunk* Next;		// pointer to the next free chunk
// 	struct FreeChunk* Prev;		// pointer to the previous free chunk
// } FreeChunk;

// // defining a global variable to store of the head of the above defined doubly-linked-list
// FreeChunk* FreeList = NULL;   

// void *memalloc(unsigned long requestedSize) 
// {
// 	printf("memalloc() called\n");
// 	if(requestedSize == 0) return NULL;

// 	// Search for a free chunk of memory that is large enough to accomdate the request
// 	FreeChunk* current = FreeList;

// 	// The free memory chunk should also have space to accomodate the metadata
// 	unsigned long sizeNeeded = requestedSize + ALLOC_MEM_METADATA;  

// 	while(current != NULL) {
// 		// We encounter a free memory chunk that is large enough to cover requested size + 8 bytes(metadata)
// 		if(current->Size >= sizeNeeded && current->Size >= FREE_MEM_METADATA) {

// 			// Remove the chunk from free memory
// 			if(current->Prev != NULL) current->Prev->Next = current->Next;
// 			else FreeList = current->Next;
// 			if(current->Next != NULL) current->Next->Prev = current->Prev;

// 			// Check if the size of the memory to be allocated requires some padding or not
// 			unsigned long paddingSize = (ALLOC_MEM_METADATA - (sizeNeeded)%(ALLOC_MEM_METADATA) )%ALLOC_MEM_METADATA;

// 			// Check if the new size is less than 24 Bytes
// 			if(sizeNeeded + paddingSize < FREE_MEM_METADATA) paddingSize = FREE_MEM_METADATA - sizeNeeded;

// 			// Calculate the remaining size after allocation
// 			unsigned long remSize = current->Size - (sizeNeeded + paddingSize);

// 			if(remSize < FREE_MEM_METADATA) {
// 				// Include these remaining bytes as padding in the allocated memory chunk
// 				*((unsigned long*)current) = current->Size;
// 				return (void*)((char*)current + ALLOC_MEM_METADATA);
// 			}
// 			else {
// 				// Define the portion of the chunk which is allocated
// 				FreeChunk* allocatedChunk = current;
// 				allocatedChunk->Size = current->Size - remSize;

// 				// Define the poriton of the chunk which is free
// 				FreeChunk* remainingChunk = (FreeChunk*)((char*)allocatedChunk + allocatedChunk->Size);
// 				remainingChunk->Size = remSize;
// 				remainingChunk->Next = FreeList;
// 				remainingChunk->Prev = NULL;

// 				// Add this free chunk at the top the free list
// 				if(FreeList != NULL) FreeList->Prev = remainingChunk;
// 				FreeList = remainingChunk;

// 				return (void*)((char*)allocatedChunk + ALLOC_MEM_METADATA);
// 			}
// 		}
// 		// If the free memory chunk is not large enough, keep iterating
// 		current = current->Next;
// 	}

// 	// If no suitable free chunk is found, request a new chunk from the OS
// 	unsigned long rem = sizeNeeded%FOUR_MB;
// 	unsigned long factor = ( (sizeNeeded) / (FOUR_MB) ); 	
// 	factor += (rem != 0 ? 1:0);
// 	unsigned long sizeRequestedFromOS = (factor)*(FOUR_MB);

// 	// Maximum memory that can be allocated using mmap() call is about 4GB, which is also about the limit of unsigned long, which is our imput limit
// 	void* allocatedMemory = mmap(NULL, sizeRequestedFromOS, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
// 	if(allocatedMemory == MAP_FAILED) return NULL;

// 	// 8-byte padding
// 	unsigned long paddingSize = (ALLOC_MEM_METADATA - (sizeNeeded)%(ALLOC_MEM_METADATA) )%ALLOC_MEM_METADATA;

// 	// Check if the new size is less than 24 Bytes
// 	if(sizeNeeded + paddingSize < FREE_MEM_METADATA) paddingSize = FREE_MEM_METADATA - sizeNeeded;

// 	// Initialize the size in the allocated memory	
// 	*((unsigned long*)allocatedMemory) = sizeNeeded + paddingSize;

// 	// Add the new chunk to the free list
// 	FreeChunk* newFreeChunk = (FreeChunk*)((char*)allocatedMemory + sizeNeeded + paddingSize);
// 	newFreeChunk->Size = sizeRequestedFromOS - sizeNeeded - paddingSize;
// 	newFreeChunk->Next = FreeList;
// 	newFreeChunk->Prev = NULL;

// 	if(FreeList != NULL) FreeList->Prev = newFreeChunk;
	
// 	FreeList = newFreeChunk;
	
// 	return (void*)((char*)allocatedMemory + ALLOC_MEM_METADATA);
// }

// int memfree(void *ptr)
// {
// 	printf("memfree() called\n");
// 	if(ptr == NULL) return -1;

// 	// Get the size of the memory chunk which is to be freed
// 	unsigned long sizeFreed = *((unsigned long*)((char*)ptr - ALLOC_MEM_METADATA));

// 	printf("free size at start of list = %lu MB\n",(FreeList->Size + 8)/(1024*1024));
	
// 	// Create a free chunk with this size
// 	FreeChunk* freedChunk = (FreeChunk*)((char*)ptr - ALLOC_MEM_METADATA);
// 	freedChunk->Size = sizeFreed;
// 	freedChunk->Next = freedChunk->Prev = NULL;

// 	// Find the chunk immediately before and after the freed chunk
// 	FreeChunk* prev = FreeList;
// 	FreeChunk* next = FreeList;
// 	int isLeftFree = 0, isRightFree = 0;

// 	printf("Address of ptr = %p\n", &ptr);

// 	// Let us check if there is a free chunk to the right of the memory being freed
// 	while(next != NULL) {
// 		if( (char*)ptr + sizeFreed - ALLOC_MEM_METADATA == (char*)next ) {
// 			isRightFree = 1;
// 			printf("next free chunk at %p\n",&next);
// 			break;
// 		}
// 		next = next->Next;
// 	}

// 	// Let us check if there is a free chunk to the left of the memory being fr eed
// 	while(prev != NULL) {
// 		if( (char*)prev + prev->Size + ALLOC_MEM_METADATA == (char*)ptr ) {
// 			isLeftFree = 1;
// 			printf("prev free chunk at %p\n",&prev);
// 			break;
// 		}
// 		prev = prev->Next;
// 	}

// 	if(prev != NULL) {
// 		printf("Size of prev free chunk is %lu MB\n",(prev->Size+8)/(1024*1024));
// 		// printf("is prev free chunk contigous =  %d\n", (char*)prev + prev->Size - ALLOC_MEM_METADATA == (char*)ptr );
// 		// printf("%lu\n", (char*)ptr - ((char*)prev + prev->Size - ALLOC_MEM_METADATA) );
// 	}
// 	if(next != NULL){
// 		printf("Size of next free chunk is %lu MB\n",(next->Size+8)/(1024*1024));
// 		// printf("is next free chunk contigous =  %d\n", (char*)ptr + sizeFreed - ALLOC_MEM_METADATA == (char*)next );
// 	} 

// 	// Case 1: Continugous memory chunks on both sides are allocated
// 	if ((prev == NULL || !isLeftFree) && (next == NULL || !isRightFree)) {
// 		printf("both sides allocated\n");
// 		freedChunk->Next = FreeList;
// 		if (FreeList != NULL) {
// 			FreeList->Prev = freedChunk;
// 		}
// 		FreeList = freedChunk;
// 	}

// 	// Case 2: Contiguous memory chunk on the right side is free
// 	else if ((prev == NULL || !isLeftFree) && (next != NULL && isRightFree)) {
//         // Coalesce 'f' and the memory chunk on the right side
// 		printf("right side free\n");
//         freedChunk->Size += next->Size;
//         freedChunk->Next = next->Next;
//         if (next->Next != NULL) {
//             next->Next->Prev = freedChunk;
//         }
//         if (FreeList == next) {
//             FreeList = freedChunk;
//         }
//     }

// 	// Case 3: Contiguous memory chunk on the left side is free
//     else if ((prev != NULL && isLeftFree) && (next == NULL || !isRightFree)) {
//         // Coalesce 'f' and the memory chunk on the left side
// 		printf("left side free\n");
//         prev->Size += freedChunk->Size;
//         prev->Next = next;
//         if (next != NULL) {
//             next->Prev = prev;
//         }
//         if (FreeList == freedChunk) {
//             FreeList = prev;
//         }
//     }

// 	// Case 4: Contiguous memory chunks on both sides are free
//     else {
//         // Coalesce 'f' with both the memory chunks on the left and right sides
// 		printf("both sides free\n");
//         prev->Size += freedChunk->Size + next->Size;
//         prev->Next = next->Next;
//         if (next->Next != NULL) {
//             next->Next->Prev = prev;
//         }
//         if (FreeList == freedChunk) {
//             FreeList = prev;
//         }
//     }

// 	return 0;
// }	

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

// defining macros for the metadata size for allocated and free memory chunks
#define ALLOC_MEM_METADATA 8
#define FREE_MEM_METADATA 24

unsigned long FOUR_MB = 4*1024*1024;

// defining a structure to store the free memory chunk
typedef struct FreeChunk {
	unsigned long Size;				// size of the chunk
	struct FreeChunk* Next;		// pointer to the next free chunk
	struct FreeChunk* Prev;		// pointer to the previous free chunk
} FreeChunk;

// defining a global variable to store of the head of the above defined doubly-linked-list
FreeChunk* FreeList = NULL;

void *memalloc(unsigned long requestedSize) 
{
	printf("memalloc() called\n");
	if(requestedSize == 0) return NULL;

	// Search for a free chunk of memory that is large enough to accomdate the request
	FreeChunk* current = FreeList;

	// The free memory chunk should also have space to accomodate the metadata
	unsigned long sizeNeeded = requestedSize + ALLOC_MEM_METADATA;  

	while(current != NULL) {
		// We encounter a free memory chunk that is large enough to cover requested size + 8 bytes(metadata)
		if(current->Size >= sizeNeeded && current->Size >= FREE_MEM_METADATA) {

			// Remove the chunk from free memory
			if(current->Prev != NULL) current->Prev->Next = current->Next;
			else FreeList = current->Next;
			if(current->Next != NULL) current->Next->Prev = current->Prev;

			// Check if the size of the memory to be allocated requires some padding or not
			unsigned long paddingSize = (ALLOC_MEM_METADATA - (sizeNeeded)%(ALLOC_MEM_METADATA) )%ALLOC_MEM_METADATA;

			// Check if the new size is less than 24 Bytes
			if(sizeNeeded + paddingSize < FREE_MEM_METADATA) paddingSize = FREE_MEM_METADATA - sizeNeeded;

			// Calculate the remaining size after allocation
			unsigned long remSize = current->Size - (sizeNeeded + paddingSize);

			if(remSize < FREE_MEM_METADATA) {
				// Include these remaining bytes as padding in the allocated memory chunk
				*((unsigned long*)current) = current->Size;
				return (void*)((char*)current + ALLOC_MEM_METADATA);
			}
			else {
				// Define the portion of the chunk which is allocated
				FreeChunk* allocatedChunk = current;
				allocatedChunk->Size = current->Size - remSize;

				// Define the poriton of the chunk which is free
				FreeChunk* remainingChunk = (FreeChunk*)((char*)allocatedChunk + allocatedChunk->Size);
				remainingChunk->Size = remSize;
				remainingChunk->Next = FreeList;
				remainingChunk->Prev = NULL;

				// Add this free chunk at the top the free list
				if(FreeList != NULL) FreeList->Prev = remainingChunk;
				FreeList = remainingChunk;

				return (void*)((char*)allocatedChunk + ALLOC_MEM_METADATA);
			}
		}
		// If the free memory chunk is not large enough, keep iterating
		current = current->Next;
	}

	// If no suitable free chunk is found, request a new chunk from the OS
	unsigned long rem = sizeNeeded%FOUR_MB;
	unsigned long factor = ( (sizeNeeded) / (FOUR_MB) ); 	
	factor += (rem != 0 ? 1:0);
	unsigned long sizeRequestedFromOS = (factor)*(FOUR_MB);

	// Maximum memory that can be allocated using mmap() call is about 4GB, which is also about the limit of unsigned long, which is our imput limit
	void* allocatedMemory = mmap(NULL, sizeRequestedFromOS, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if(allocatedMemory == MAP_FAILED) return NULL;

	// 8-byte padding
	unsigned long paddingSize = (ALLOC_MEM_METADATA - (sizeNeeded)%(ALLOC_MEM_METADATA) )%ALLOC_MEM_METADATA;

	// Check if the new size is less than 24 Bytes
	if(sizeNeeded + paddingSize < FREE_MEM_METADATA) paddingSize = FREE_MEM_METADATA - sizeNeeded;

	// Initialize the size in the allocated memory	
	*((unsigned long*)allocatedMemory) = sizeNeeded + paddingSize;

	// Add the new chunk to the free list
	FreeChunk* newFreeChunk = (FreeChunk*)((char*)allocatedMemory + sizeNeeded + paddingSize);
	newFreeChunk->Size = sizeRequestedFromOS - sizeNeeded - paddingSize;
	newFreeChunk->Next = FreeList;
	newFreeChunk->Prev = NULL;

	if(FreeList != NULL) FreeList->Prev = newFreeChunk;
	
	FreeList = newFreeChunk;
	
	return (void*)((char*)allocatedMemory + ALLOC_MEM_METADATA);
}

int memfree(void *ptr)
{
	printf("memfree() called\n");
	if(ptr == NULL) return -1;

	// Get the size of the memory chunk which is to be freed
	unsigned long sizeFreed = *((unsigned long*)((char*)ptr - ALLOC_MEM_METADATA));
	
	// Create a free chunk with this size
	FreeChunk* freedChunk = (FreeChunk*)((char*)ptr - ALLOC_MEM_METADATA);
	freedChunk->Size = sizeFreed;
	freedChunk->Next = freedChunk->Prev = NULL;

	// Find the chunk immediately before and after the freed chunk
	FreeChunk* prev = FreeList;
	FreeChunk* next = FreeList;
	int isLeftFree = 0, isRightFree = 0;

	// Let us check if there is a free chunk to the right of the memory being freed
	while(next != NULL) {
		if( (char*)ptr + sizeFreed - ALLOC_MEM_METADATA == (char*)next ) {
			isRightFree = 1;
			break;
		}
		next = next->Next;
	}

	// Let us check if there is a free chunk to the left of the memory being freed
	while(prev != NULL) {
		if( (char*)prev + prev->Size + ALLOC_MEM_METADATA == (char*)ptr ) {
			isLeftFree = 1;
			break;
		}
		prev = prev->Next;
	}

	// Case 1: Continugous memory chunks on both sides are allocated
	if ((prev == NULL || !isLeftFree) && (next == NULL || !isRightFree)) {
		freedChunk->Next = FreeList;
		if (FreeList != NULL) {
			FreeList->Prev = freedChunk;
		}
		FreeList = freedChunk;
	}

	// Case 2: Contiguous memory chunk on the right side is free
	else if ((prev == NULL || !isLeftFree) && (next != NULL && isRightFree)) {
        // Coalesce 'f' and the memory chunk on the right side
        freedChunk->Size += next->Size;
        freedChunk->Next = next->Next;
        if (next->Next != NULL) {
            next->Next->Prev = freedChunk;
        }
        if (FreeList == next) {
            FreeList = freedChunk;
        }
    }

	// Case 3: Contiguous memory chunk on the left side is free
    else if ((prev != NULL && isLeftFree) && (next == NULL || !isRightFree)) {
        // Coalesce 'f' and the memory chunk on the left side
        prev->Size += freedChunk->Size;
        prev->Next = next;
        if (next != NULL) {
            next->Prev = prev;
        }
        if (FreeList == freedChunk) {
            FreeList = prev;
        }
    }

	// Case 4: Contiguous memory chunks on both sides are free
    else {
        // Coalesce 'f' with both the memory chunks on the left and right sides
        prev->Size += freedChunk->Size + next->Size;
        prev->Next = next->Next;
        if (next->Next != NULL) {
            next->Next->Prev = prev;
        }
        if (FreeList == freedChunk) {
            FreeList = prev;
        }
    }

	return 0;
}	
