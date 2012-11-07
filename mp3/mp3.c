#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/vmalloc.h>

#include "mp3_given.h"
MODULE_LICENSE("GPL");

#define DELAY_PERIOD  (HZ/20)
#define DEVICE_NAME "node"

#define proc_rootName "mp3"
#define proc_statusName "status"

static struct proc_dir_entry* proc_root;
static struct proc_dir_entry* proc_status;
//PCB 
static struct task_control_block
{
	struct list_head list;
	
	struct task_struct *linux_task;
	unsigned int pid;

	unsigned long jiffies;
	unsigned long cpu_utilization;
	unsigned long major_fault_count, minor_fault_count;
} tasks;

//forward declaration for called back functions
static int device_open(struct inode *inode, struct file *file);
static int device_release(struct inode *inode, struct file *file);
static int device_mmap(struct file *file,struct vm_area_struct *vma);
//file_operation for the character device
static struct file_operations fops = {
   .owner = THIS_MODULE,
   .open = device_open,
   .release = device_release,
   .mmap = device_mmap
};
//lock for the PCB list
static spinlock_t lock;

static struct delayed_work worker;
unsigned long* memory_buffer;
static unsigned int tick_counter;

int device_major;
int buffer_count;


/*-----------------------------------------------------------------------*/
//work function that is embedded to the worker. This function 
//computes cpu_use, and page fault counts for each registered process
//and writes the accumulated values into the mapped buffer
static void work_function(struct work_struct * work) {
	tick_counter++;
	if (tick_counter < 12000){
		//re-schedule
		INIT_DELAYED_WORK(&worker, &work_function);
		schedule_delayed_work(&worker, DELAY_PERIOD);
	}
	
	struct list_head* node;
	struct task_control_block* task = NULL;
	
	unsigned long major, minor, cpu, tmajor, tminor, tcpu;
	
	unsigned long jiff = jiffies;
	major = minor = cpu = tmajor = tminor = tcpu = 0;
	
	spin_lock(&lock);
	list_for_each(node, &tasks.list) {
		task = list_entry(node, struct task_control_block, list);
		get_cpu_use(task->pid, &minor, &major, &cpu);
		
		tminor += minor;
		tmajor += major;
		tcpu += cpu;
	}
	spin_unlock(&lock);
	
	printk(KERN_INFO "%d %d %d %d %d %d", DELAY_PERIOD, tick_counter, jiff, tminor, tmajor, tcpu);
	
	//save data in the buffer
	memory_buffer[buffer_count++] = jiff;
	memory_buffer[buffer_count++] = tminor;
	memory_buffer[buffer_count++] = tmajor;
	memory_buffer[buffer_count++] = tcpu;
	
	//end the buffer by a -1
	//memory_buffer[buffer_count] = -1;
	
	
}

static int device_open(struct inode *inode, struct file *file)
{
   return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
   return 0;
}

//mmap() called back used to map virtual address space vma into the 
//dedicated buffer
static int device_mmap(struct file *file,struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	//unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page, pos;

	pos = 0;

	while(size > 0){
		page = vmalloc_to_pfn(((char *) memory_buffer) + pos);

		if (remap_pfn_range(vma,start, page, PAGE_SIZE, vma->vm_page_prot))  //PAGE_SHARED?
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

//register a process
void rregistration(int pid) {
	struct task_control_block* temp;
	int was_empty;

	was_empty = list_empty(&tasks.list);
	
	//initialize a new task_control_block
	temp = (struct task_control_block*) kmalloc(sizeof(struct task_control_block), GFP_KERNEL);
	temp->pid = pid;
	temp->linux_task = find_task_by_pid(temp->pid);
	
	//add the new task_control_block into the list
	spin_lock(&lock);
	list_add(&(temp->list), &tasks.list);
	spin_unlock(&lock);
	
	//create the worker if this is the first task_control_block
	if (was_empty) {
		//reset the buffer
		buffer_count = 0;
		//reset the counter
		tick_counter = 0;
		
		INIT_DELAYED_WORK(&worker, &work_function);
		schedule_delayed_work(&worker, DELAY_PERIOD);
	}
		
}
	
//deregister a process
void dderegistration(int pid) {
	struct list_head* node1, * node2;
	struct task_control_block* entry;
	
	spin_lock(&lock);
	list_for_each_safe(node1, node2, &tasks.list) {
		entry = list_entry(node1, struct task_control_block, list);
		if (entry->pid != pid) continue;
		list_del(node1);
		kfree(entry);
		
		//also delete the work if the deleted task_control_block is the last one
		if (list_empty(&tasks.list)) {
			cancel_delayed_work_sync(&worker);
			
			memory_buffer[buffer_count] = -1;
		}
		
		break;
	}
	spin_unlock(&lock);
}

//read call back when a process reads from the proc 
//writes the list of registered processes to the result
int read_func(char* buffer, char** buffer_location, off_t offset, int length, int* eof, void* data) {
	struct list_head* node;
	struct task_control_block* task;
	char temp[101];
	int total = 0, len = 0;;
	
	spin_lock(&lock);
	list_for_each(node, &tasks.list) {
		task = list_entry(node, struct task_control_block, list);
		len = sprintf(temp, "%d\n", task->pid);
		memcpy(buffer + total, temp, len);
		total+=len;
	}
	spin_unlock(&lock);
	
	return total;
}

void tokenize(char ** tokens, char * string) {
	int n = 0, i = 0;
	int len = strlen(string);
	for (i = 0; i < len; i++) {
		if (string[i] != ' ' && (i == 0 || string[i-1] == '\0')) {
			tokens[n++] = string + i;
		}
		else if (string[i] == ' ') {
			string[i] = '\0';
		}
	}
}

//write call back when a process write to the proc
//this function calls registration or deregistration of 
//the process based on the values written
int write_func(struct file* file, const char * buffer, unsigned long length, void * data) {
	char s[101]; 
	char* tokens[5];
	long int pid = 0;
	
	if ( copy_from_user(s, buffer, length)) {
		return -EFAULT;
	}
	s[length] = '\0';
	
	tokenize(tokens, s);
	kstrtol(tokens[1], 10, &pid); //PID
	
	if (strcmp(tokens[0], "R") == 0) {
		rregistration(pid);
	}
	else if (strcmp(tokens[0], "U") == 0) {
		dderegistration(pid);
	}
	
	return length;
}

//starts the module and takes care of initialization logic
int init_module(void) {
	
	//create procfs root
	proc_root = proc_mkdir(proc_rootName, NULL);
	proc_status = create_proc_entry(proc_statusName, 0666, proc_root);
	proc_status->read_proc = read_func;
	proc_status->write_proc = write_func;
	
	//initialize the lock
	spin_lock_init(&lock);
	
	//initialize linked list 
	spin_lock(&lock);
	INIT_LIST_HEAD(&tasks.list);
	spin_unlock(&lock);
	
	//initialize the memory buffer
	memory_buffer = (unsigned long*) vmalloc(128*PAGE_SIZE); //128 pages
	
	//register the char device
	device_major = register_chrdev(0, DEVICE_NAME, &fops);
	printk(KERN_INFO "I was assigned major number %d. To talk to\n", device_major);
	printk(KERN_INFO "the drive, create a dev file with\n");
	printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, device_major);
	
	printk(KERN_INFO "Module is now loaded.\n");
	
	return 0;
}

//stops the module and does cleanup
void cleanup_module(void) {
	struct list_head* node1, * node2;
	struct task_control_block* entry;
	
	//remove procfs root
	remove_proc_entry(proc_statusName, proc_root);
	remove_proc_entry(proc_rootName, NULL);
	
	//free the linked list
	spin_lock(&lock);
	list_for_each_safe(node1, node2, &tasks.list) {
		entry = list_entry(node1, struct task_control_block, list);
		list_del(node1);
		kfree(entry);
	}
	spin_unlock(&lock);
	
	//free the memory buffer
	vfree(memory_buffer);
	
	//remove the char device
	unregister_chrdev(device_major, DEVICE_NAME);
	
	printk(KERN_INFO "Module is now unloaded.\n");
}

