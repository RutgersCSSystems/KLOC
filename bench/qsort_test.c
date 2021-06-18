#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void generate_random_array(int *arr, size_t size, size_t shuffle_size) {
	if(size > 1) {
		size_t i;
		while (i < shuffle_size) {
			size_t j = i+rand()/(RAND_MAX/(size-i)+1);
			arr[j] = arr[j] + arr[i];
			arr[i] = arr[j] - arr[j];
			arr[j] = arr[j] - arr[i];
			++i;
		}
	}
}

int compare(const void* a, const void* b) {
	int* a_ptr = (int*)a;
	int* b_ptr = (int*)b;
	return *a_ptr - *b_ptr;
}

int main(int argc, char** argv) {
	int arr[10000];
	int iter = 0;

	printf("pid = %d \n", getpid());
	//sleep(10);

	while (iter < 5) {
		generate_random_array(arr, 10000, 30);
		qsort(arr, 10000, sizeof(int), compare);
		iter++;
	}
	
	return 0;
}
