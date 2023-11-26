#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

#define MAX_PATH_LEN 4096

// function to calculate file size
unsigned long getFileSize(const char *file_path) {
	struct stat file_stat;
	if(stat(file_path, &file_stat) < 0) {
		printf("Unable to execute\n");
		exit(-1);
	}
	return (unsigned long)file_stat.st_size;
}

// function to calculate directory size
unsigned long getDirSize(const char *dir_path) {

	unsigned long dirSize = 0;
	// let us first add the size of the  directory itself
	dirSize += getFileSize(dir_path);

	DIR *dir = opendir(dir_path);

	// Error Handling
	if(dir == NULL) {
		printf("Unable to execute\n");
		exit(-1);
	}

	// creating a pipe to communicate between the parent and child processes
	int pipe_fd[2];
	if(pipe(pipe_fd) < 0) {
		printf("Unable to execute\n");
		exit(-1);
	}

	struct dirent *nextDir;
	// Loop over all the files/directories present in the current directory
	while( (nextDir = readdir(dir)) ) {

		// Skip "." and ".." entries
		if( strcmp(nextDir->d_name,".") == 0 || strcmp(nextDir->d_name,"..") == 0) continue;

		// copy the path of the sub-directory
		char entry_path[MAX_PATH_LEN];
		if(snprintf(entry_path,sizeof(entry_path),"%s/%s",dir_path,nextDir->d_name) < 0) {
			printf("Unable to execute\n");
			exit(-1);
		}

		// finding info about the new entry, using lstat instead of stat, because we may encounter symbolic links
		struct stat entry_stat;
		if(lstat(entry_path, &entry_stat) < 0) {
			printf("Unable to execute\n");
			exit(-1);
		}

		// if the new entry is a symbolic link, we need to resolve it and find its size
		if(S_ISLNK(entry_stat.st_mode)) {
			char target_path[MAX_PATH_LEN];
            if (readlink(entry_path, target_path, sizeof(target_path)) < 0) {
				printf("Unable to execute\n");
				exit(-1);
            }
            dirSize += getDirSize(target_path);
		}

		// if the new entry is a directory, create a child process to calculate its size
		else if(S_ISDIR(entry_stat.st_mode)) {
			pid_t child_pid = fork();
			// error handling
			if(child_pid < 0) {
				printf("Unable to execute\n");
				exit(-1);
			}
			// child process
			else if(child_pid == 0) {
				// first, we will close the read end of the pipe for the child process
				close(pipe_fd[0]);	
				unsigned long subDirSize = getDirSize(entry_path);
				// second, we will send the size of the subdirectory by writing to the parent process using the pipe
				write(pipe_fd[1],&subDirSize,sizeof(unsigned long));
				close(pipe_fd[1]);
				exit(0);
			}
		}
		// if the new entry is a file, simply add its size
		else if(S_ISREG(entry_stat.st_mode)) {
			dirSize += getFileSize(entry_path);
		}
	}
	close(pipe_fd[1]);
	// read the sizes of the subdirectories from child processes and add them to the total
	unsigned long subDirSizeTemp;
	while(read(pipe_fd[0],&subDirSizeTemp,sizeof(unsigned long)) > 0) {
		dirSize += subDirSizeTemp;
	}
	close(pipe_fd[0]);
	closedir(dir);
	return dirSize;
}

int main(int argc, char **argv)
{
	// if there are not the correct number of inputs on the command line
	if(argc != 2) {
		printf("Unable to execute\n");
		exit(-1);
	}
	const char *dir_path = argv[1];
	unsigned long total_size = getDirSize(dir_path);
	printf("%lu\n",(unsigned long)total_size);
	return 0;
}
