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
* data sample to be retrieved at each iteration
*/
struct data_sample
{
   long jiff; 
   long major_pf;
   long minor_pf;
   long cpu_used;
};

/*
* augmented process control block used by the 
* scheduler
*/
struct mp3_task_struct
{
   struct list_head list;

   struct task_struct *linux_task;
   
   unsigned int pid;
 
};

static int device_open(struct inode*,struct file *);
static int device_release(struct inode*,struct file *);
static int device_mmap(struct file *file,struct vm_area_struct *vm_area);

#endif
