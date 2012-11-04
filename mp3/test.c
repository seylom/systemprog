#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>  
#include <string.h>

#define FIX_N 1000
#define MAX_LINE_SIZE 200

struct timeval init;
int COUNT;

double millies(struct timeval begin, struct timeval end) {
	return (end.tv_sec - begin.tv_sec) * 1000.0 + (end.tv_usec - begin.tv_usec) / 1000.0;
}

int factorial(int n) {
	int fac = 1;
	int i;
	for (i = 0; i < n; i++) fac = (fac * i)% 1000000;
	return fac;
}

int approximately_count(int length) {
	struct timeval start, end;
	int n = 0;
	gettimeofday(&start, NULL);
	end = start;
	while (millies(start, end) < length) {
		factorial(FIX_N);
		n++;
		gettimeofday(&end, NULL);
	}
	return n;
}

int do_job(int iteration) {
	struct timeval time;
	gettimeofday(&time, NULL);
	printf("start %d-th interation of %d at %d\n",iteration,getpid(),millies(init, time));

	int result;
	int i;
	for (i = 0; i < COUNT; i++) {
		result = (result + factorial(FIX_N)) % 1000000;
	}
	gettimeofday(&time, NULL);
	printf("stop process %d at %d\n",getpid(),millies(init, time));

	return result;
}


int main(int arg, char ** args) {
	if (arg!=4) {
		printf("Usage: %s period(ms) computation(ms) loops\n",args[0]);
		return 1;
	}
	
    char buff[400];
	int i;
    int r_pid,r_period,r_compu;
	FILE * fp;
    char pid_string[127];
	long pid = getpid();
    int accepted = 0;

	int period = atoi(args[1]);
	int computation = atoi(args[2]);
	int loops = atoi(args[3]);
	
	//register
	sprintf(pid_string, "echo \"R,%d,%d,%d\">/proc/mp2/status", (long)pid,(long)period,(long)computation);
    system(pid_string);

   //verify the process was accepted
   fp = fopen("/proc/mp2/status", "r+"); 

   if (!fp) 
      return;
      
   while (!feof(fp)){
        
        if (!fgets (buff, MAX_LINE_SIZE, fp))
            continue;
        
        if(sscanf(buff,"%d:%d,%d",&r_pid,&r_period,&r_compu)== 3)
        {
            if (r_pid == pid)
            {
               accepted = 1;
               break;
            }
        }    
   }
   
   fclose(fp);

   if (accepted!=1)
   {
      printf("Registration DENIED for pid # %d\n",pid); 
      
      return;
   }

   printf("Registration ACCEPTED for pid # %d : period: %d computation: %d\n",pid,period,computation);
	
	printf("Preparing necessary stuff\n");
	//count the number of times needed to compute factorial in order to match 'computation'
	COUNT = approximately_count(computation);

	printf("Process pid: %d Number of jobs = %d\n",getpid(),COUNT);
	
	printf("Yield process %d\n",pid);
    sprintf(pid_string, "echo \"Y,%d\">/proc/mp2/status", pid);
	system(pid_string);

	printf("Main loop\n");
	gettimeofday(&init, NULL);
	for ( i = 0; i < loops; i++) {
		do_job(i);
		
		//yield
        sprintf(pid_string, "echo \"Y,%d\">/proc/mp2/status", pid);
		system(pid_string);

	}
	
	//deregister
	sprintf(pid_string, "echo \"D,%d\">/proc/mp2/status", pid);
    system(pid_string);
	
	printf("Deregistration completed for pid %d\n",pid);
	
	return 0;
}	
