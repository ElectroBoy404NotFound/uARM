#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <asm/page.h>
#include <asm/page.h>
#include "types.h"


/* Magic values */

	#define NUM_MINORS		16	//passed to alloc_disc
	#define DISK_NAME		"pvd"

	#define DRIVER_NAME		"pvd"
	#define DRIVER_DESC		"pvDisk"

/* Globals */

	DEFINE_SPINLOCK(g_slock);		//our lock
	static struct gendisk* g_disk;		//the disk device
	static struct task_struct* g_thread = 0;//our thread

	static unsigned long g_numSec, g_secSz;


#define PVD_INFO_NUM_SECTORS		0
#define PVD_INFO_SECTOR_SZ		1

#define CALL_SETUP			4
#define CALL_ACCESS			5

#define SETUP_OP_INFO			0
#define SETUP_OP_READ			1
#define SETUP_OP_WRITE			2

#define ACCESS_OP_READ			0
#define ACCESS_OP_WRITE			1

extern unsigned long _sys_pvd_call(unsigned long r0,  unsigned long r1,  unsigned long r2, unsigned long r12);

static int _sys_pvd_getinfo(int infoType, unsigned long* infoVal){
	
	_sys_pvd_call(SETUP_OP_INFO, infoType, 0, CALL_SETUP);
	*infoVal = _sys_pvd_call(0, 0, ACCESS_OP_READ, CALL_ACCESS);

	return 0;
}

static int _sys_pvd_read_sec(unsigned long sec, void* buf){
	
	int i;
	unsigned long *p = buf;

	_sys_pvd_call(SETUP_OP_READ, sec, 0, CALL_SETUP);
	for(i = 0; i < g_secSz / sizeof(unsigned long); i++) *p++ = _sys_pvd_call(0, i, ACCESS_OP_READ, CALL_ACCESS);

	return 0;
}

static int _sys_pvd_write_sec(unsigned long sec, const void* buf){
	
	int i;
	const unsigned long *p = buf;

	for(i = 0; i < g_secSz / sizeof(unsigned long); i++) _sys_pvd_call(*p++, i, ACCESS_OP_WRITE, CALL_ACCESS);
	_sys_pvd_call(SETUP_OP_WRITE, sec, 0, CALL_SETUP);

	return 0;
}

static unsigned long pvd_scale(unsigned long val_, unsigned long scale_factor){
	unsigned long long val = val_;

	val <<= 9;
	do_div(val, scale_factor);

	return val;
}

static int pvd_io(char* buf, unsigned long sec, unsigned long num, int op){
	unsigned long sec_sz = g_secSz, num_sec = g_numSec, i;
	int ret = 0;

	sec = pvd_scale(sec, sec_sz);
	num = pvd_scale(num, sec_sz);

	for(i = 0; i < num && !ret; i++, sec++, buf += sec_sz){
		if(sec >= num_sec){
			ret = -EIO;
			break;
		}

		if(op == 1){
			ret = _sys_pvd_write_sec(sec, buf);
		}
		else if(op == 0){
			ret = _sys_pvd_read_sec(sec, buf);
		}
		else{
			return -ENOTSUPP;
		}
	}

	return ret;
}

static int pvd_thread(void* unused){

	struct request_queue *q = g_disk->queue;
	struct request *req = NULL;
	char* buf;
	unsigned long sec, num;
	int ret;

	current->flags |= PF_MEMALLOC;		//just in case :)

	spin_lock_irq(q->queue_lock);

	while (!kthread_should_stop()) {
		
		if (!req && !(req = blk_fetch_request(q))) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(q->queue_lock);
			schedule();
			spin_lock_irq(q->queue_lock);
			continue;
		}

		spin_unlock_irq(q->queue_lock);

		if (blk_fs_request(req)) {

			buf = req->buffer;
			sec = blk_rq_pos(req);
			num = blk_rq_cur_sectors(req);
			switch(rq_data_dir(req)){
				case READ:
					ret = pvd_io(buf, sec, num, 0);
					rq_flush_dcache_pages(req);
					break;
				case WRITE:
					rq_flush_dcache_pages(req);
					ret = pvd_io(buf, sec, num, 1);
					break;
				default:
					ret = -EIO;
					break;
			}
		}
		else{
			sec = num = -1;
			ret = -EIO;
		}

		spin_lock_irq(q->queue_lock);
		nDEBUG("FTL DBG: ending request for %ld+%ld: %d\n", sec, num, ret);
		if(!__blk_end_request_cur(req, ret)) req = NULL;
		nDEBUG("FTL DBG: done with request %s of %ld+%ld\n", sec, num);
	}

	if (req) __blk_end_request_all(req, -EIO);

	spin_unlock_irq(q->queue_lock);

	return 0;
}

static void pvd_request(struct request_queue *q){

	wake_up_process(g_thread);
}

static int pvd_ioctl(struct block_device* dev, fmode_t mode, unsigned int cmd, unsigned long arg){

	int ret = 0;

	if(cmd == HDIO_GETGEO){

		struct hd_geometry geo;

		geo.cylinders = 1;	//it is unsigned short so we'd never really return a good value here - might as well just do this
		geo.heads = 4;
		geo.sectors = 16;
		if (copy_to_user((void __user *) arg, &geo, sizeof(geo))) ret = -EFAULT;
	}
	else ret = -ENOTTY;

	return ret;
}


static struct block_device_operations disk_ops = {
	.owner =	THIS_MODULE,
	.ioctl =	pvd_ioctl,
};

static int __init pvd_init(void){

	int major, ret = 0;
	struct request_queue* queue;
	UInt32 sec_sz, num_sec;

	ret = _sys_pvd_getinfo(PVD_INFO_SECTOR_SZ, &sec_sz);
	if(ret) goto out1;

	ret = _sys_pvd_getinfo(PVD_INFO_NUM_SECTORS, &num_sec);
	if(ret) goto out1;

	g_numSec = num_sec;
	g_secSz = sec_sz;

	pr_err("PVD: found device with %ld sectors of %ld bytes\n", num_sec, sec_sz);

	major = register_blkdev(0, DRIVER_NAME);
	if (major <= 0){
		nERROR("PVD: failed to allocate a major number: %d\n", major);
		ret = -ENOMEM;
		goto out1;
	}
	nTRACE("PVD: Allocated major number %d for PVD device\n", major);

	g_disk = alloc_disk(NUM_MINORS);
	if(!g_disk){
		nERROR("PVD: failed to allocate a gen_disk\n");
		ret = -ENOMEM;
		goto out2;
	}

	queue = blk_init_queue(pvd_request, &g_slock);
	if (!queue){
		nERROR("PVD: failed to allocate a request queue\n");
		ret = -ENOMEM;
		goto out3;
	}

	blk_queue_bounce_limit(queue, BLK_BOUNCE_HIGH);

	g_disk->major = major;
	g_disk->first_minor = 0;
	strcpy(g_disk->disk_name, DISK_NAME);
	g_disk->fops = &disk_ops;	//todo:
	g_disk->queue = queue;
	g_disk->flags = 0;

	blk_queue_logical_block_size(queue, sec_sz);
	set_capacity(g_disk, (((loff_t)num_sec) * ((loff_t)sec_sz)) >> 9);

	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, queue);	//we're not a rotary medium - do not waste time reordering requests
	
	g_thread = kthread_run(pvd_thread, NULL, "[pvd_worker]");
	if(IS_ERR(g_thread)){
		ret = PTR_ERR(g_thread);
		goto out4;
	}

	add_disk(g_disk);

	return 0;
out4:
out3:
	del_gendisk(g_disk);
out2:
	unregister_blkdev(major, DRIVER_NAME);
out1:
	return ret;
}

static void __exit pvd_exit(void){

	int major = g_disk->major;
	struct request_queue* queue = g_disk->queue;

	
	kthread_stop(g_thread);

	del_gendisk(g_disk);
	put_disk(g_disk);
	blk_cleanup_queue(queue);
	unregister_blkdev(major, DRIVER_NAME);
}


module_init(pvd_init);
module_exit(pvd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Grinberg, 2010");
MODULE_DESCRIPTION(DRIVER_DESC);


