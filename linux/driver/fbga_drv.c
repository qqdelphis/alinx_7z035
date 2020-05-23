#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/of_address.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include "fbga_drv.h"

//unsigned myint = 0xdeadbeef;
//char *mystr = "default";

//module_param(myint, int, S_IRUGO);
//module_param(mystr, charp, S_IRUGO);

static struct fasync_struct *fb_async = NULL;
struct fbga_drv *fb_drv;

int fbga_drv_open(struct inode * inode, struct file *filp)
{
    printk("device is open!\n");
    return 0;
}

int fbga_drv_release(struct inode *inode, struct file *filp)
{
    printk("device is release!\n");
    return 0;
}

static ssize_t fbga_drv_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos)
{
    unsigned int count = size;
    unsigned int ret = 0;

    if(copy_to_user(buf, (void*)(fb_drv->vaddr), count))
    {
	    ret = -EINVAL;
    }
    
    return ret;
}

static ssize_t fbga_drv_write(struct file *filp,const char __user *buf, size_t size, loff_t *ppos)
{
    //unsigned int count = size;
    unsigned int rdata;
    unsigned int ret = 0;
    char buf_tmp[16];
    memset(buf_tmp,0,16);

    if(copy_from_user((char *)buf_tmp, buf, 16))
    {
	    ret = -EINVAL;
    }

    printk("\nbuf_tmp[0]=0x%x\n",(unsigned int)buf_tmp[0]);
    printk("\n");
    
    
    iowrite32((unsigned int)buf_tmp[0], fb_drv->vaddr);

    rdata=ioread32(fb_drv->vaddr);
    printk("read mem after copy_from_user, rdata = %x\n\n", rdata);
    
    return ret;
}

static int fbga_fasync (int fd, struct file *filp, int on)
{
    printk("\n\ndriver: fbga_fasync\n\n");
    return fasync_helper(fd, filp, on, &fb_async);
}

static long fbga_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    switch(cmd)
    {
        case CASE0:
//            fs4412_beep_on();
            break;
        case CASE1:
//            fs4412_beep_off();
            break;
        case CASE2:
//            beep_freq( arg );
            break;
        default :
            return -EINVAL;
    }
    return 0;
}

static const struct file_operations fbga_drv_fops=
{
    .owner = THIS_MODULE,
    .open = fbga_drv_open,
    .release = fbga_drv_release,
    .read = fbga_drv_read,
    .write = fbga_drv_write,
    .fasync = fbga_fasync,
    .unlocked_ioctl = fbga_ioctl,
};


static irqreturn_t fbga_drv_irq(int irq, void *lp)
{
	printk("fbga_drv interrupt triggered\n");
    kill_fasync(&fb_async, SIGIO, POLL_IN);
	printk("fbga_drv interrupt handled\n");
	return IRQ_HANDLED;
}

static int fbga_drv_probe(struct platform_device *pdev)
{
    struct resource r_mem;
    struct device_node *np;
    
    struct device *dev = &pdev->dev;
    //struct axidma_local *lp = NULL;
    
	int rc = 0;
    
    //unsigned long rdata;

	dev_info(dev, "Device Tree Probing\n");
    
    //memset(tmp_buffer,0,sizeof(tmp_buffer));

	fb_drv = kmalloc(sizeof(struct fbga_drv), GFP_KERNEL);
	if (fb_drv == NULL)
	{
		dev_err(dev, "unable to allocate device structure\n");
		return -ENOMEM;
	}

    /* Get reserved memory region from Device-tree */
    np = of_parse_phandle(dev->of_node, "memory-region", 0);
    if (!np) {
        dev_err(dev, "No %s specified\n", "memory-region");
        goto error_handle1;
    }
  
    rc = of_address_to_resource(np, 0, &r_mem);
    if (rc) {
        dev_err(dev, "No memory address assigned to the region\n");
		return -EINVAL;
    }
  
    fb_drv->paddr = (void*)r_mem.start;

    printk("\n\nStart map the paddr and vaddr\n\n");

    fb_drv->vaddr = memremap(r_mem.start, resource_size(&r_mem), MEMREMAP_WB);
    if(!fb_drv->vaddr) 
    {
        dev_err(dev, "cannot map the mem\n");
		goto error_handle1;
    }

    dev_info(dev, "Allocated reserved memory, vaddr: 0x%p, paddr: 0x%p\n", fb_drv->vaddr, fb_drv->paddr);

	rc =alloc_chrdev_region(&fb_drv->devno,0, 1,DEVICE_NAME);
	if (rc < 0)
	{
		dev_err(&pdev->dev, "unable to alloc chrdev \n");
		goto error_handle2;
	}

    
	cdev_init(&fb_drv->fb_cdev, &fbga_drv_fops);

	fb_drv->fb_cdev.owner = THIS_MODULE;

	fb_drv->fb_cdev.ops = &fbga_drv_fops;

	rc = cdev_add(&fb_drv->fb_cdev,fb_drv->devno,1);
    if(rc < 0){
		dev_err(&pdev->dev, "unable to do cdev add \n");
        goto error_handle3;
    }
    
	fb_drv->fb_class = class_create(THIS_MODULE, DEVICE_NAME);
	
    rc=device_create(fb_drv->fb_class, NULL, MKDEV(MAJOR(fb_drv->devno), 0), NULL, DEVICE_NAME);
    //rc = device_create(fb_drv->fb_class, &pdev->dev, MKDEV(MAJOR(fb_drv->devno), 0), NULL, DEVICE_NAME);
	//if(rc <0){
	//	dev_err(&pdev->dev, "unable to create device \n");
    //    goto error_handle4;
    //}
        
    fb_drv->irq = platform_get_irq(pdev,0);
    if (fb_drv->irq <= 0)
        return -EINVAL;
    
    fb_drv->pdev = pdev;
    
    rc = request_threaded_irq(fb_drv->irq, NULL,
            fbga_drv_irq,
            IRQF_TRIGGER_RISING | IRQF_ONESHOT,
            DEVICE_NAME, NULL);
    if (rc) {
        printk(KERN_ALERT "irq_probe irq error=%d\n", rc);
        goto error_handle5;
    }
    else
    {
        printk("\nirq = %d\n", fb_drv->irq);
    }


    dev_info(&pdev->dev, "fbga drv added successfully\n");
    
    return 0;

error_handle1:
    iounmap(fb_drv->vaddr);
    return -EINVAL;

error_handle2:
    iounmap(fb_drv->vaddr);
    kfree(fb_drv);
    return -EINVAL;

error_handle3:
    iounmap(fb_drv->vaddr);
    kfree(fb_drv);
    cdev_del(&fb_drv->fb_cdev);
    return -EINVAL;

//error_handle4:
//    iounmap(fb_drv->vaddr);
//    kfree(fb_drv);
//    class_destroy(fb_drv->fb_class);
//    cdev_del(&fb_drv->fb_cdev);
//	unregister_chrdev_region(fb_drv->devno, 1);
//    return -EINVAL;

error_handle5:
    free_irq(fb_drv->irq,NULL);
    iounmap(fb_drv->vaddr);
    kfree(fb_drv);
    class_destroy(fb_drv->fb_class);
    cdev_del(&fb_drv->fb_cdev);
	unregister_chrdev_region(fb_drv->devno, 1);
    return -EINVAL;
}

static int fbga_drv_remove(struct platform_device *pdev)
{
	//struct axidma_local *lp = dev_get_drvdata(dev);
	//free_irq(lp->irq, lp);
	device_destroy(fb_drv->fb_class,MKDEV(MAJOR(fb_drv->devno),0));

    iounmap(fb_drv->vaddr);

    if(fb_drv){
	    kfree(fb_drv);
    }

    class_destroy(fb_drv->fb_class);
    cdev_del(&fb_drv->fb_cdev);
	unregister_chrdev_region(fb_drv->devno, 1);
	return 0;
}

static struct of_device_id fbga_drv_of_match[] = {
	{ .compatible = "vendor,fbga_drv", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, fbga_drv_of_match);

static struct platform_driver fbga_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= fbga_drv_of_match,
	},
	.probe		= fbga_drv_probe,
	.remove		= fbga_drv_remove,
};

static int __init fbga_drv_init(void)
{
	printk("Hello module world.\n");

	return platform_driver_register(&fbga_driver);
}

static void __exit fbga_drv_exit(void)
{
	platform_driver_unregister(&fbga_driver);
	printk(KERN_ALERT "Goodbye module world.\n");
}

module_init(fbga_drv_init);
module_exit(fbga_drv_exit);

MODULE_AUTHOR("CHENGL");
MODULE_DESCRIPTION("FBGA_DRV");
MODULE_LICENSE("GPL");