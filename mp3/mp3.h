#ifndef _MP2_GROUP1_H
#define _MP2_GROUP1_H

#define DIR_NAME "mp3"
#define STATUS_NAME "status"
#define MOD_TIMER(timer) mod_timer(&timer, get_jiffies_64() + msecs_to_jiffies(5000))
#define MAX_CHAR 120

#define PROCESS_SLEEPING   0
#define PROCESS_RUNNING    1
#define PROCESS_READY      2

static char written_data[MAX_CHAR];
static struct proc_dir_entry *proc_dir, *status;

static spinlock_t list_lock; 

/*
* augmented process control block used by the 
* scheduler
*/
struct mp3_task_struct
{
   struct list_head list;

   struct task_struct *linux_task;
   
   unsigned int pid;
   
   long process_utilization;
   long major_fault_count;
   long minor_fault_count;
 
};


#endif
