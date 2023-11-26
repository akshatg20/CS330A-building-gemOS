/* Checking with system call for different number of arguments by applying filter to some syscalls*/

#include<ulib.h>

// Check working with fork() system calls
int main (u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
	int strace_fd = create_trace_buffer(O_RDWR);
	if(strace_fd != 3){
		printf("create trace buffer failed\n");
		return -1;
	}
	int rdwr_fd = create_trace_buffer(O_RDWR);
	if(rdwr_fd != 4){
		printf("create trace buffer failed for read and write\n");
		return -1;
	}
	u64 strace_buff[4096];
	int read_buff[4096];
	int write_buff[4096];

	for(int i = 0, j = 0; i< 4096; i++){
		j = i % 26;
		write_buff[i] = 'A' + j;
	}
	int ret = strace(25, 0);
	if(ret != 0) {
		printf("1. Test case failed\n");
		return -1;
	}
	ret = strace(2, 0);
	if(ret != 0) {
        printf("2. Test case failed\n");
		return -1;
    }
	ret = strace(25, 0);
	// printf("%d\n",ret);
	if(ret != -EINVAL) {
        printf("3. Test case failed\n");
		return -1;
    }
    ret = strace(10, 0);
	if(ret != 0) {
        printf("4. Test case failed\n");
		return -1;
    }
		
	start_strace(strace_fd, FILTERED_TRACING);
	int write_ret = write(rdwr_fd, write_buff, 10);
	if(write_ret != 10) {
		printf("5. Test case failed\n");
		return -1;
	}
	int read_ret = read(rdwr_fd, read_buff, 10);
	if(read_ret != 10) {
		printf("6. Test case failed\n");
		return -1;
	}
	int pid = fork();
    if(pid < 0) {
        printf("Cannot procede\n");
        return -1;
    }
    sleep(10);
	getpid();
	sleep(10);
	if(pid > 0) {
		write_ret = write(rdwr_fd, write_buff, 10);
		if(write_ret != 10) {
			printf("7. Test case failed in pid = %d\n",pid);
			return -1;
		}
		read_ret = read(rdwr_fd, read_buff, 10);
		if(read_ret != 10) {
			printf("8. Test case failed in pid = %d\n",pid);
			return -1;
		}
	}
	else {
		int fd_fork = create_trace_buffer(O_RDWR);
		write_ret = write(fd_fork, write_buff, 10);
		if(write_ret != 10) {
			printf("9. Test case failed in pid = %d\n",pid);
			return -1;
		}
		read_ret = read(fd_fork, read_buff, 10);
		if(read_ret != 10) {
			printf("10. Test case failed in pid = %d\n",pid);
			return -1;
		}
		close(fd_fork);
	}

	// What if these functions are called in forked process also ? - Won't be called as mentioned in a forum
	if(pid > 0) {
        ret = end_strace();
		if(ret < 0) {
			printf("11. Test case failed in pid = %d\n",pid);
			return -1;
		}
		int strace_ret = read_strace(strace_fd, strace_buff, 5);
        // printf("Return value from read_strace: %d\n",strace_ret);
		// printf("syscall_num at the end: %d\n",strace_buff[10]);
		if(strace_ret != 80) {
            printf("12. Test case failed in pid = %d\n",pid);
            return -1;
        }
        if(strace_buff[0] != 25){
            printf("13. Test case failed in pid = %d\n",pid);
            return -1;
        }
        if(strace_buff[2] != (u64) &write_buff){
            printf("14. Test case failed in pid = %d\n",pid);
            return -1;
        }
        if(strace_buff[4] != 10){
            printf("15. Test case failed in pid = %d\n",pid);
            return -1;
        }
		if(strace_buff[5] != 2){
            printf("16. Test case failed in pid = %d\n",pid);
            return -1;
        }
		if(strace_buff[6] != 25){
            printf("17. Test case failed in pid = %d\n",pid);
            return -1;
        }
		close(rdwr_fd);
		close(strace_fd);
    }
	
    printf("Test case passed in pid = %d\n",pid);
    return 0;
}