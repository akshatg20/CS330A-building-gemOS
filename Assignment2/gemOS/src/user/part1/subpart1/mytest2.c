#include<ulib.h>

int main (u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {

    int fd = create_trace_buffer(O_RDWR);
	char buff[400];
	char buff2[400];

	for(int i = 0; i< 400; i++){
		buff[i] = 'A';
	}

	int ret = write(fd, buff, 400);
//	printf("ret value from write: %d\n", ret);
	if(ret != 400){
		printf("1.Test case failed\n");
		return -1;
	}

	int ret2 = read(fd, buff2, 400);
//	printf("ret value from write: %d\n", ret2);
	if(ret2 != 400){
		printf("2.Test case failed\n");
		return -1;	
	}

	if(buff2[0] == buff[0]) {
		printf("3.Test case failed\n");
		return -1;
	}

	ret = write(fd,buff,400);
	if(ret != 400){
		printf("4.Test case failed\n");
		return -1;
	}

	ret2 = read(fd,buff2,350);
	if(ret2 != 350) {
		printf("5.Testcase failed\n");
		return -1;
	}
	
    close(fd);
	printf("Test case passed\n");
	return 0;
}
