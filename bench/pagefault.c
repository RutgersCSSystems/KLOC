#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

#define __NR_start_trace 333

int main(int argc, char** argv) {
	long int a = syscall(__NR_start_trace, 1);
	printf("System call returned %ld\n", a);

	if (argc < 2) {
		printf("Usage error \n");
		return 0;
	}

	int i, j, N, page_size, ints_per_page, int_size;
	N = atoi(argv[1]);

	page_size = getpagesize();
	printf("Page size: %d \n", page_size);

	int_size = (int)sizeof(int);
	printf("Signed int size : %d \n", int_size);

	ints_per_page = (int)page_size/int_size;
	printf("Entries per page: %d \n", ints_per_page);
	
	int **arr = (int **)malloc(N * sizeof(int *));
	for (int i=0; i<N; i++)
		arr[i] = (int *)malloc(ints_per_page * sizeof(int));

	int x = 0;
	
	printf("matrix insert \n");

	for (i=0; i<ints_per_page; i++) {
		for(j=0; j<N; j++) {
			x += arr[j][i];
		}
	}

	printf("After insert \n");

	for (i=0; i<N; i++)
		free(arr[i]);
	free(arr);

	long int b = syscall(__NR_start_trace, 0);
	printf("System call exit returned %ld\n", b);

	return 0;
}
