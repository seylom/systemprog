#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <asm/uaccess.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "mp3.h"
#include "mp3_given.h"

static struct mp3_task_struct process_list;
static struct task_struct *dispatcher_thread;
static struct mp3_task_struct *running_task;

/*
 * _timer_callback: interrupt function called when the timer expires
 * restarts the timer and wakes up the worker function
 */
void _timer_callback(unsigned long pid)
{
	struct list_head* node;
	struct mp3_task_struct* task = NULL;

	spin_lock(&lock);

	list_for_each(node, &process_list.list) {
		task = list_entry(node, struct mp3_task_struct, list);
		if (task->pid == pid) { 
			//printk(KERN_INFO "Timer tick for pid %d",task->pid);
			//printk(KERN_INFO "pid %d is ready",task->pid);
	
			wake_up_process(dispatcher_thread);
	
			break;
		}
	}
	spin_unlock(&lock);
}

/*
*Register a process using provided parameters if
*the addition of the process doesn't overload the CPU
*
*/
void register_process(unsigned int pid)
{ 
   struct mp3_task_struct *task ;
   
   printk(KERN_INFO "REGISTER process with pid: %d",pid);
   
   task = (struct mp3_task_struct *)kmalloc(sizeof(struct mp3_task_struct),GFP_KERNEL);
   
   //initialize the task
   task->linux_task = find_task_by_pid(pid);
   
   task->pid = pid; 
 
   //setup the timer
   
   /*
   setup_timer(&task->wakeup_timer, _timer_callback, (unsigned long)task->pid); 
   task->last_wakeup_time = get_jiffies_64();
   
   if(mod_timer(&task->wakeup_timer, task->last_wakeup_time + msecs_to_jiffies(task->period)))
   {
      return;
   }
   */
   
   //add the task to the list of processes
   spin_lock(&lock);
   list_add(&(task->list),&(process_list.list));
   spin_unlock(&lock);
}


/*
* Find an augmented process control block using the
* provided pid
*
*/
struct mp3_task_struct* task_by_pid(int pid)
{
   struct mp3_task_struct *tmp;
   struct list_head *pos, *q;
   
   tmp = NULL;
   
   spin_lock(&lock);
   list_for_each_safe(pos, q, &process_list.list)
   {
      
      tmp = list_entry(pos, struct mp3_task_struct, list);
      
      if (tmp->pid == pid)
         break;
   }
   spin_unlock(&lock);
  
   return tmp;
}

/*
* Deregister the process using the provided pid
*
*/
void deregister_process(unsigned int pid)
{
   struct mp3_task_struct *tmp;
   struct list_head *pos, *q;
   
   // look up the process
   spin_lock(&lock);
   list_for_each_safe(pos, q, &process_list.list)
   {
      
      tmp = list_entry(pos, struct mp3_task_struct, list);
      
      if (tmp->pid == pid)
      {
         if (running_task!= NULL && running_task->pid == tmp->pid)
         {
            running_task = NULL;
         }
         
         list_del(pos);
         kfree(tmp);
         
         printk(KERN_INFO "DEREGISTER task with pid %d", pid);
              
         break;
      }  
   }
   spin_unlock(&lock);
}

/**
 * status_write: called when the /proc/mp2/status file is written to
 * gathers the buffer from the user space, converts the char array into an integer,
 * only if the integer is a valid process id, gets the cpu time for the process and
 * stores it in the linked list
 */
int status_write(struct file *file, const char *buf, unsigned long count, void *data)
{

   int ret = 0;
   const char delimiters[] = ",";
   char *running;
   char *token;
   
   unsigned int pid;
   int period, computation;
   
   pid = 0;
   period = 0;
   computation = 0;
   
   if(count > MAX_CHAR)
   {
      count = MAX_CHAR;
   }
   if(copy_from_user(written_data, buf, count))
   {
      return -EFAULT;
   }
   
   running = (char*)kmalloc(sizeof(char)*count, GFP_KERNEL);
   
   written_data[count] = '\0';
   strcpy(running,(const char*)&written_data[0]);
   
   //retrieve the first token in this case we expect R, D or Y
   token = strsep(&running,delimiters);
   
   if ((int)strlen((const char*)token) != 1)
     return count;
      
   switch(token[0])
   {
      case 'R':
         {
            //register the provided process
            //extract process information R, PID
           
            token = strsep(&running,delimiters);
            ret = kstrtouint((const char*)token, 10, &pid);
             
            if (ret)
               pid = 0;
               
            register_process(pid);

         };
         break;
      case 'U':
         {
            //unregister the provided process
            //retrieve second token: pid
            token = strsep(&running,delimiters);
            
            ret = kstrtouint((const char*)token, 10, &pid);
            if (ret)  
               pid = 0;
               
            deregister_process(pid);
               
         };
         break;
   }
   
   kfree(running);

   return count;
}
/*
 * status_read:  called when file /proc/mp1/status is read
 * loops through list of pids and corresponding cpu times, copying them to the buffer returned to the user
 */
int status_read(char *buf, char ** start, off_t offset, int count, int *eof, void *data)
{
   struct mp3_task_struct *tmp;
   struct list_head *pos;
   char process_seq[MAX_CHAR];
   char *tmp_seq = process_seq, *cur_buf = buf;
   unsigned int len = 0, total_len = 0;

   spin_lock(&lock);
   list_for_each(pos, &process_list.list)
   {
      tmp = list_entry(pos, struct mp3_task_struct, list);
      sprintf(process_seq, "%d\n", tmp->pid);
      while(*tmp_seq)
      {
         ++len;
         ++tmp_seq;
      }
      memcpy(cur_buf, process_seq, len);
      total_len += len;
      cur_buf += len;
      tmp_seq = process_seq;
      len = 0;
   }
   spin_unlock(&lock);
   
   return total_len;
}



/*
*
* Dispatcher thread handler
* Finds the task with the highest priority
* and performs a context switch
*/
int dispatcher_thread_handler(void *data)
{
/*
   //find next task to run
   struct mp3_task_struct *tmp,*new_task;
   struct list_head *pos;
   struct sched_param sparam_old,sparam_new;
   
   new_task = NULL;
   
   set_current_state(TASK_INTERRUPTIBLE);
   while(!kthread_should_stop())
   {
	  //find the task with the highest priority (lowest period)
      spin_lock(&lock);
      list_for_each(pos, &process_list.list)
      {
         tmp = list_entry(pos, struct mp3_task_struct, list);
         
         if ((tmp->process_state == PROCESS_READY) &&
             ((new_task == NULL) || 
             (tmp->period < new_task->period))
            )
         {
            new_task = tmp;
         }
      }
      spin_unlock(&lock);
      
      if (new_task != running_task)
      {
		 //if we had a running task with a lower priority, update its scheduling policy
         if (running_task)
         {
            if (running_task->process_state == PROCESS_RUNNING)
               running_task->process_state = PROCESS_READY;
         
            if (running_task->linux_task) {
               sparam_old.sched_priority=0;
               sched_setscheduler(running_task->linux_task, SCHED_NORMAL, &sparam_old);
            }
         }
         
	     // Update new highest process priority
         if (new_task) {
            
            new_task->process_state = PROCESS_RUNNING;
            
            wake_up_process(new_task->linux_task);
            sparam_new.sched_priority=MAX_USER_RT_PRIO-1;
            sched_setscheduler(new_task->linux_task, SCHED_FIFO, &sparam_new);
            
            printk(KERN_INFO "pid %d is preempted by %d",running_task->pid,new_task->pid);
            
            running_task = new_task;
         }
      } 
      
      schedule();
      set_current_state(TASK_INTERRUPTIBLE);
   }
   set_current_state(TASK_RUNNING);
  */ 
   return 0;
}

/*
 * my_module_init: called when the module is initialized in the kernel
 * initializes the list of pids and /proc/ entries, creates and starts
 * the timer, and creates and starts the worker thread
 */
int __init my_module_init(void)
{
   
   char thread_name[7] = "worker";
   INIT_LIST_HEAD(&process_list.list);
   
   proc_dir = proc_mkdir(DIR_NAME, NULL);
   status = create_proc_entry(STATUS_NAME, 0666, proc_dir);

   status->read_proc = status_read;
   status->write_proc = status_write;
   
   spin_lock_init(&lock);
   
   running_task  = NULL;

   dispatcher_thread = kthread_create(&dispatcher_thread_handler, NULL, thread_name);
   
   if (dispatcher_thread) 
   {
		wake_up_process(dispatcher_thread);
   }
   
   return 0;
}

/*
 * my_module_exit: called upon the removal of the kernel module
 * deallocates each entry of the pid list, deregsiteres the /proc/
 * entries, and stops the worker thread
 */
void __exit my_module_exit(void)
{
   
   struct mp3_task_struct *tmp;
   struct list_head *pos, *q;

   spin_lock(&lock);
   list_for_each_safe(pos, q, &process_list.list)
   {
      
      tmp = list_entry(pos, struct mp3_task_struct, list);
        
      printk(KERN_INFO "removing pcb for task with pid %d", tmp->pid);    
      
      list_del(pos);
      kfree(tmp);
   }
   spin_unlock(&lock);
   
   if(kthread_stop(dispatcher_thread))
   {
      return;
   }
   
   remove_proc_entry(STATUS_NAME, proc_dir);
   remove_proc_entry(DIR_NAME, NULL);
}

//Register init and exit functions
module_init(my_module_init);
module_exit(my_module_exit);

//THIS IS REQUIRED BY THE KERNEL
MODULE_LICENSE("GPL");
