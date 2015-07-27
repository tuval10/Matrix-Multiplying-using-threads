#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/wait.h> // for wait macros etc
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <fcntl.h> // for open flags
#include <assert.h>
#include <limits.h>
#include <string.h>

/************* globals **************/  
#define BITSIZE				sizeof(char)

int ** mat1;		// first   matrix
int ** mat2; 		// second  matrix
int ** mat3; 		// result  matrix
int n, threadNum; 	// matrix dimension and number of threads

// mutex and cond variable
pthread_mutex_t sharedElementMutex; // mutex for access to shared "data"

// our shared predicates and "data"
int sharedRow  = 0; // row index shared with cnsumers
int done = 0; // used to signal consumers they should exit gracefuly
/************************************/  

long timevaldiff(struct timeval *start_time, struct timeval *end_time){
	long milisec, seconds, useconds;
	seconds  = end_time->tv_sec  - start_time->tv_sec;
	useconds = end_time->tv_usec - start_time->tv_usec;
	milisec = ((seconds) * 1000 + useconds/1000.0) + 0.5;
	return milisec;
}

int calcCij(int row, int col){
	int i, j, sum=0;
	for(i=0; i<n; ++i)
		sum += mat1[row][i] * mat2[i][col];
	return sum;
}

void calcRow(int row){
	int col;
	for(col = 0 ; col < n ; ++col)
		mat3[row][col]= calcCij(row,col);
} 

void thread_mode1(int thread_index){
	int row = thread_index;
	do{
		calcRow(row);
		row += threadNum;
	} while( row < n);
}

void thread_mode2(){
	int rc, row;
   	while (!done) { 
		// lock get row and increase row number
	   	assert( (rc = pthread_mutex_lock(&sharedElementMutex)) == 0);
		row = sharedRow++;
		if(sharedRow == n) //check if we reached the end
			done = 1;
	   	assert( (rc = pthread_mutex_unlock(&sharedElementMutex)) == 0);
		//calculate - no need for lock
		calcRow(row);
   	}
}

void thread_mode3(){
	int rc, row;
   	while ( (row = __sync_fetch_and_add(&sharedRow,1)) < n){ 
		calcRow(row);
   	}
}

void * thread_mode(void * modep){
	int mode, thread_index;
	long mtime;
	struct timeval start, end;
	memcpy(&mode, modep, sizeof(int));
	assert(mode >=1 && mode <=3);
	if(mode == 1) memcpy(&thread_index, modep + sizeof(int) , sizeof(int));
	assert( gettimeofday(&start, NULL) != -1 && "Error: gettimeofday failure");
	switch(mode){
	case 1:
		thread_mode1(thread_index);
		break;
	case 2:
		thread_mode2();
		break;
	case 3:
		thread_mode3();
		break;
	}
	assert( gettimeofday(&end, NULL) != -1 && "Error: gettimeofday failure");
	mtime = timevaldiff(&start, &end);
	printf("Time: %ld msec for thread: %u\n", mtime, (unsigned) pthread_self() );
}

void calc_mode0(){
	int row;
	for(row = 0 ; row < n ; ++row)
		calcRow(row);
} 

void calc_mode1(){
	int threadIndex, rc, arr[2];
	pthread_t * theradList;
	arr[0] = 1;
	assert ( (theradList = (pthread_t *) calloc (threadNum, sizeof(pthread_t *))) != NULL);
	for(threadIndex = 0 ; threadIndex < threadNum ; ++threadIndex){
		arr[1] = threadIndex;
		assert( ! (rc = pthread_create(&theradList[threadIndex], NULL, thread_mode, (void *) arr)) );
	}
    	// join waits for the threads to finish
	for(threadIndex = 0 ; threadIndex < threadNum ; ++threadIndex)
		assert( ! (rc = pthread_join(theradList[threadIndex], NULL)) ); 
	free(theradList);
} 

void calc_mode2(){
	int rc = 0, threadIndex, mode = 2;
	pthread_t * theradList;
	assert ( (theradList = (pthread_t *) calloc (threadNum, sizeof(pthread_t *))) != NULL);
	/* init mutex and cond variable */
   	rc = pthread_mutex_init(&sharedElementMutex, NULL); 	assert(rc==0);

    	// create threads
	for(threadIndex = 0 ; threadIndex < threadNum ; ++threadIndex)
		assert( ! (rc = pthread_create(&theradList[threadIndex], NULL, thread_mode, (void *) &mode)) );
    	// join waits for the threads to finish
	for(threadIndex = 0 ; threadIndex < threadNum ; ++threadIndex)
		assert( ! (rc = pthread_join(theradList[threadIndex], NULL)) ); 
   	pthread_mutex_destroy(&sharedElementMutex);
	free(theradList);
}

void calc_mode3(){
	int threadIndex, rc, mode = 3;
	pthread_t * theradList;
	assert ( (theradList = (pthread_t *) calloc (threadNum, sizeof(pthread_t *))) != NULL);
	for(threadIndex = 0 ; threadIndex < threadNum ; ++threadIndex)
		assert( ! (rc = pthread_create(&theradList[threadIndex], NULL, thread_mode, (void *) &mode)) );
    	// join waits for the threads to finish
	for(threadIndex = 0 ; threadIndex < threadNum ; ++threadIndex)
		assert( ! (rc = pthread_join(theradList[threadIndex], NULL)) );
	free(theradList);
} 

void calc_mode(int mode){
	struct timeval start, end;
	long mtime;
	assert(mode>=0 && mode <=3);
	assert( gettimeofday(&start, NULL) != -1 && "Error: gettimeofday failure");
	switch(mode){
	case 0:
		calc_mode0();
		break;
	case 1:
		calc_mode1();
		break;
	case 2:
		calc_mode2();
		break;
	case 3:
		calc_mode3();
		break;
	}
	assert( gettimeofday(&end, NULL) != -1 && "Error: gettimeofday failure");
	mtime = timevaldiff(&start, &end);
	printf("Time: %ld msec total\n", mtime);
}

void init_matrixes(int fd1, int fd2){
	int i;
	//malloc n pointers
	assert ( (mat1 = (int**) malloc (sizeof(int*) * n)) != NULL);
	assert ( (mat2 = (int**) malloc (sizeof(int*) * n)) != NULL);
	assert ( (mat3 = (int**) malloc (sizeof(int*) * n)) != NULL);

	//malloc row pointers
	for (i = 0; i < n; ++i){
		assert ( (mat1[i] = (int*) malloc (sizeof(int) * n)) != NULL);
		assert ( (mat2[i] = (int*) malloc (sizeof(int) * n)) != NULL);
		//initiallized zero
		assert ( (mat3[i] = (int*) calloc (n, sizeof(int))) != NULL);
	}

	//fill row by row
	for (i = 0; i < n; ++i){
		assert(read(fd1, mat1[i], sizeof(int)*n ) >= 0);
		assert(read(fd2, mat2[i], sizeof(int)*n ) >= 0);
	}
}

void print_matrixes(){
	int i, j;
	printf("matrix 1:\n");
	for (i = 0; i < n; ++i){
		for (j = 0; j < n; ++j){
			printf("%d ", mat1[i][j]);
		}
		printf("\n");
	}
	printf("matrix 2:\n");
	for (i = 0; i < n; ++i){
		for (j = 0; j < n; ++j){
			printf("%d ", mat2[i][j]);
		}
		printf("\n");
	}
}

void print_result(){
	int i, j;
	printf("matrix 3:\n");
	for (i = 0; i < n; ++i){
		for (j = 0; j < n; ++j){
			printf("%d ", mat3[i][j]);
		}
		printf("\n");
	}
}
 
int main( int argc, char **argv){
	int i, fd1, fd2, mode, tempN;
	assert(argc == 5 || argc == 6);
	srand(time(NULL));
	threadNum = atoi(argv[1]);
	assert( (threadNum > 0 && argc == 6) || (threadNum ==0 && argc == 5) );

	//reading from files
	assert( (fd1 = open(argv[2], O_CREAT | O_RDWR , S_IRWXU)) >= 0);
	assert( (fd2 = open(argv[3], O_CREAT | O_RDWR , S_IRWXU)) >= 0);
	assert( read(fd1, &n, sizeof(int)) == sizeof(int) );
	assert( read(fd2, &tempN, sizeof(int)) == sizeof(int) );
	assert( n == tempN);
	init_matrixes(fd1, fd2);
	close(fd1); close(fd2);

	//done reading  - calculate mat1*mat2 to mat3
	if( threadNum == 0 )
		calc_mode(0);
	else{
		calc_mode( atoi(argv[5]) );	
	}
	// write result (mat3) into file
	assert( (fd1 = open(argv[4], O_CREAT | O_RDWR , S_IRWXU)) >= 0);
	assert(write(fd1, &n, sizeof(int) ) == sizeof(int) );  // write n
	for (i = 0; i < n; ++i){ 	// fill row by row
		assert(write(fd1, mat3[i], sizeof(int)*n ) >= 0);
	}
	return 0;
}
