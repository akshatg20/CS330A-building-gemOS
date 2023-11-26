#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

int main(int argc, char **argv) 	
{
	// if there are no inputs on the command line --> will lead to error
	if(argc <= 1) {
		printf("Unable to execute\n");
		exit(-1);
	}

	// the number that is to be modified will be the last argument in the input array
	// the argument passed is supposed to fit inside an unsigned long

	unsigned long val = atol(argv[argc-1]);
	val = round(sqrt(val));							// rounding off the value after taking its square root

	// now we convert the value back to a string
	char output[32];
	if(sprintf(output, "%lu", val) < 0) {
		printf("Unable to execute\n");
		exit(-1);
	}

	// pass on the result for the upcoming processes
	argv[argc-1] = output;

	// check if other processes remaining
	if(argc > 2) {
		char **new_argv = argv + 1;		// move past the current argument
		// execute the next process
		if(execv(new_argv[0],new_argv) < 0) {
			printf("Unable to execute\n");
			exit(-1);
		}
	}

	// if the process does not go into the above if block, it means there are no other processes, so print our result
	printf("%s\n",output); 
	return 0; 
}
