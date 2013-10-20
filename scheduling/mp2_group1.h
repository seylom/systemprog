#ifndef _MP2_GROUP1_H
#define _MP2_GROUP1_H

#define DIR_NAME "mp2"
#define STATUS_NAME "status"
#define MOD_TIMER(timer) mod_timer(&timer, get_jiffies_64() + msecs_to_jiffies(5000))
#define MAX_CHAR 120

#define PROCESS_SLEEPING   0
#define PROCESS_RUNNING    1
#define PROCESS_READY      2

static char written_data[MAX_CHAR];
static struct proc_dir_entry *proc_dir, *status;

static spinlock_t lock;
static long total_use = 0;


/*
* augmented process control block used by the 
* scheduler
*/
struct mp2_task_struct
{
   struct list_head list;

   struct task_struct *linux_task;
   struct timer_list wakeup_timer;
   
   unsigned int pid;
   int period;
   int computation;
   int process_state;
   
   long last_wakeup_time; 
};


#endif
