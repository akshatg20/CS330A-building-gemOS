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

int main() {
    FreeChunk* newChunk = NULL;
    printf("%ld\n",sizeof(FreeChunk));
    printf("%ld\n",sizeof(FreeChunk*));
    return 0;
}