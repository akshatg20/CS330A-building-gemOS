#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "../mylib.h"

#define NUM 4
#define _1GB (1024*1024*1024)

//Handling large allocations
int main()
{
    unsigned long MB = 1024*1024;
	char *p[NUM];
	char *q = 0;
	int ret = 0;
	int a = 0;

	p[0] = (char*)memalloc(3*MB);
    p[1] = (char*)memalloc(2*MB);
    // p[2] = (char*)memalloc(3*MB);
    // p[3] = (char*)memalloc(2*MB);
	
	for(int i = 0; i < 1; i++)
	{
		ret = memfree(p[i]);
		if(ret != 0)
		{
			printf("2.Testcase failed\n");
			return -1;
		}
	}

	printf("Testcase passed\n");
	return 0;
}


