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
#include <linux/mm.h>
#include <linux/cdev.h>

#include <linux/workqueue.h>
#include <linux/fs.h>
#include "mp3.h"
#include "mp3_given.h"

#define DELAYED_WORK_EXPIRE (HZ/20)
#define DEVICE_NAME "node"

static struct mp3_task_struct pcb_list;
static struct delayed_work pcb_work;
static unsigned char* buffer;
static unsigned int tick_counter;

static dev_t my_dev_maj_min;
static struct cdev my_cdev;

static struct file_operations fops = {
   .owner = THIS_MODULE,
   .open = device_open,
   .release = device_release,
   .mmap = device_mmap
};

/*
* Handler for our single workqueue item. it gathers cpu utilization as well as 
* page fault information and saves it in the buffer variable.
*
*/
static void pcb_wq_function(struct work_struct *work)
{
   struct mp3_task_struct *tmp;
   struct list_head *pos, *q;

   unsigned long jiff,min_pf,maj_pf,cpu_used,
               tot_min_pf,tot_maj_pf,tot_cpu_used;
   unsigned int pid;
   
   //reset data for accuracy
   jiff = 0;
   min_pf = 0;
   maj_pf = 0;
   cpu_used = 0;
   tot_min_pf = 0;
   tot_maj_pf = 0;
   tot_cpu_used = 0;
    
   tmp = NULL;
   jiff = jiffies; 
   
   spin_lock(&list_lock);
   list_for_each_safe(pos, q, &pcb_list.list)
   {

      tmp = list_entry(pos, struct mp3_task_struct, list);

      pid = tmp->linux_task->pid;
      //get cpu utilization as well as minimum and maximum 
      //page fault
      get_cpu_use(pid,&min_pf,&maj_pf,&cpu_used);
      
      tot_min_pf +=min_pf;
      tot_maj_pf +=maj_pf;
      tot_cpu_used += cpu_used;
      
   }
   spin_unlock(&list_lock);
   
   //save data in the buffer
   
   memcpy(buffer,&jiff,sizeof(jiff));
   buffer+= sizeof(jiff);
   memcpy(buffer,&tot_min_pf,sizeof(tot_min_pf));
   buffer+= sizeof(tot_min_pf);
   memcpy(buffer,&tot_maj_pf,sizeof(tot_maj_pf));
   buffer+= sizeof(tot_maj_pf);
   memcpy(buffer,&tot_cpu_used,sizeof(tot_cpu_used));
   buffer+= sizeof(tot_cpu_used);
   
   
   tick_counter++;
   
   if (tick_counter < 12000){
      //re-schedule
      schedule_delayed_work(&pcb_work,DELAYED_WORK_EXPIRE);
   }
}

/*
*Register a process using provided parameters if
*the addition of the process doesn't overload the CPU
*
*/
void register_process(unsigned int pid)
{
   struct mp3_task_struct *task ;
   bool schedule_work;

   schedule_work = false;

   printk(KERN_INFO "REGISTER process with pid: %d",pid);

   task = (struct mp3_task_struct *)kmalloc(sizeof(struct mp3_task_struct),GFP_KERNEL);

   //initialize the task
   task->linux_task = find_task_by_pid(pid);
   task->pid = pid;

   //add the task to the list of pcb
   //schedule work if necessary

   spin_lock(&list_lock);

   if (list_empty(&pcb_list.list))
   {
      schedule_work = true;
   }

   list_add(&(task->list),&(pcb_list.list));

   if (schedule_work)
   {
      INIT_DELAYED_WORK(&pcb_work,&pcb_wq_function);
      schedule_delayed_work(&pcb_work,DELAYED_WORK_EXPIRE);
   }

   spin_unlock(&list_lock);
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

   spin_lock(&list_lock);
   list_for_each_safe(pos, q, &pcb_list.list)
   {

      tmp = list_entry(pos, struct mp3_task_struct, list);

      if (tmp->pid == pid)
         break;
   }
   spin_unlock(&list_lock);

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
   spin_lock(&list_lock);
   list_for_each_safe(pos, q, &pcb_list.list)
   {

      tmp = list_entry(pos, struct mp3_task_struct, list);

      if (tmp->pid == pid)
      {
         list_del(pos);
         kfree(tmp);

         printk(KERN_INFO "DEREGISTER task with pid %d", pid);

         if (list_empty(&(pcb_list.list)))
         {
            cancel_delayed_work_sync(&pcb_work);
         }

         break;
      }
   }
   spin_unlock(&list_lock);
}



/**
 * status_write: called when the /proc/mp3/status file is written to
 * gathers the buffer from the user space, converts the char array into an integer,
 * only if the integer is a valid process id and stores it in the linked list
 */
int status_write(struct file *file, const char *buf, unsigned long count, void *data)
{

   int ret = 0;
   const char delimiters[] = ",";
   char *running;
   char *token;

   unsigned int pid;

   pid = 0;

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

   spin_lock(&list_lock);
   list_for_each(pos, &pcb_list.list)
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
   spin_unlock(&list_lock);

   return total_len;
}

static int device_open(struct inode *inode, struct file *file)
{
   return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
   return 0;
}


/*
* maps a kernel buffer to user space in order to
* copy data.
*
*/
static int device_mmap(struct file *file,struct vm_area_struct *vma)
{
   unsigned long start = vma->vm_start;
   unsigned long size = vma->vm_end - vma->vm_start;
   //unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
   unsigned long page, pos;

   pos = 0;

   while(size > 0){
      page = vmalloc_to_pfn(buffer+pos);

      if (remap_pfn_range(vma,start,page,PAGE_SIZE,vma->vm_page_prot))  //PAGE_SHARED?
         return -EAGAIN;

      start+= PAGE_SIZE;
      pos += PAGE_SIZE;

      if (size > PAGE_SIZE){
         size -= PAGE_SIZE;
      }
      else{
         size = 0;
      }
   }

   return 0;
}


/*
 * my_module_init: called when the module is initialized in the kernel
 * initializes the list of pids and /proc/ entries, creates and starts
 * the timer, and creates and starts the worker thread
 */
int __init my_module_init(void)
{
   int rc;
   int err;
   dev_t dev;

   INIT_LIST_HEAD(&pcb_list.list);

   proc_dir = proc_mkdir(DIR_NAME, NULL);
   status = create_proc_entry(STATUS_NAME, 0666, proc_dir);

   status->read_proc = status_read;
   status->write_proc = status_write;

   spin_lock_init(&list_lock);

   buffer = (unsigned char*)vmalloc(128*PAGE_SIZE);

   //register our character device

   rc = alloc_chrdev_region(&dev,0,1,DEVICE_NAME);

   if (rc)
      return -1;

   my_dev_maj_min = MKDEV(MAJOR(dev), 0);

   cdev_init(&my_cdev,&fops);
   my_cdev.dev = my_dev_maj_min;
   my_cdev.owner = fops.owner;

   err = cdev_add(&my_cdev,my_dev_maj_min,1);

   if (err){
      printk(KERN_ERR "unable to register char device\n");

      goto err_cdev;
   }

   printk(KERN_INFO "char device %d registered\n",MAJOR(dev));

   return 0;

   /*
   //the old way
   Major = register_chrdev(0,DEVICE_NAME,&fops);

   if (Major<0) {
      printk(KERN_INFO "registering char device failed with %d\n",Major);
      return Major;
   }

   printk(KERN_INFO "I was assigned major number %d. To talk to\n",Major);
   printk(KERN_INFO "the drive, create a dev file with\n");
   printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME,Major);
   */

err_cdev:
   cdev_del(&my_cdev);
   unregister_chrdev_region(my_dev_maj_min,1);

   return err;
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
   
   cancel_delayed_work(&pcb_work);

   spin_lock(&list_lock);
   list_for_each_safe(pos, q, &pcb_list.list)
   {

      tmp = list_entry(pos, struct mp3_task_struct, list);

      printk(KERN_INFO "removing pcb for task with pid %d", tmp->pid);

      list_del(pos);
      kfree(tmp);
   }
   spin_unlock(&list_lock);
   
   vfree(buffer);

   printk(KERN_INFO "unregistering char device %d ...",MAJOR(my_dev_maj_min));
   cdev_del(&my_cdev);
   unregister_chrdev_region(my_dev_maj_min,1);

   remove_proc_entry(STATUS_NAME, proc_dir);
   remove_proc_entry(DIR_NAME, NULL);
}

//Register init and exit functions
module_init(my_module_init);
module_exit(my_module_exit);

//THIS IS REQUIRED BY THE KERNEL
MODULE_LICENSE("GPL");
