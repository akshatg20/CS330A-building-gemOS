#include <stdio.h>
#include <unistd.h>
#include "../mylib.h"

#define _1GB (1024*1024*1024)

//check metadata is maintained properly and allocation happens from correct location

int main()
{
	char *p = 0;
	char *q = 0;
	char *r = 0;
	char *s = 0;
	// unsigned long size = 0;
	int ret = 0;
	
	p = (char *)memalloc(32);
	if(p == NULL)
	{
		printf("1.Testcase failed\n");
		return -1;
	}

	q = (char *)memalloc(10);
	if(q == NULL)
	{
		printf("2.Testcase failed\n");
		return -1;
	}

	r = (char *)memalloc(32);
	if(r == NULL)
	{
		printf("3.Testcase failed\n");
		return -1;
	}

	ret = memfree(q);
	if(ret != 0)
	{
		printf("4.Testcase failed\n");
		return -1;
	}

	// ret = memfree(p);
	// if(ret != 0)
	// {
	// 	printf("5.Testcase failed\n");
	// 	return -1;
	// }

	// if(r != p + 80) {
	// 	printf("6.Testcase failed\n");
	// 	return -1;
	// }

	// ret = memfree(q);
	// if(ret != 0)
	// {
	// 	printf("6.Testcase failed\n");
	// 	return -1;
	// }

	s = (char *)memalloc(16);
	if(s == NULL)
	{
		printf("6.Testcase failed\n");
		return -1;
	}

	if(s != p + 40) {
		printf("%lu\n",s-p);
		printf("7.Testcase failed\n");
		return -1;
	}

	printf("Testcase passed\n");
	return 0;
}

// int main() {
// 	char *p = 0;
// 	char *q = 0;
// 	unsigned long size1 = 0, size2 = 0;
// 	unsigned long MB = 1024*1024;
// 	p = (char *)memalloc(4*MB - 8);
// 	if(p == NULL) {
// 		printf("1.Testcase Failed\n");
// 		return -1;
// 	}
// 	q = (char *)memalloc(1);
// 	if(q == NULL) {
// 		printf("2.Testcase Failed\n");
// 		return -1;
// 	}
// 	size1 = *((unsigned long*)p - 1);
// 	if(size1 != 4*MB) {
// 		printf("3.Testcase failed\n");
// 		return -1;
// 	}
// 	size2 = *((unsigned long*)q - 1);
// 	if(size2 != 24) {
// 		printf("4.Testcase failed\n");
// 		return -1;
// 	}
// 	if(q != p + 4*MB) {
// 		printf("5.Testcase Failed\n");
// 		// printf("%lu\n",(q-p)/(MB));
// 		return -1;
// 	}
// 	printf("Testcase passed\n");
// 	return 0;
// }