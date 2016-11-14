/********************************************************************
 *This is flash ioctl for  reading ,writing and erasing application
 *******************************************************************/

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <asm/system.h>

#include <atheros.h>
#include "ath_flash.h"


/* this is passed in as a boot parameter by bootloader */
extern int __ath_flash_size;

/*
 * IOCTL Command Codes
 */
#define AR7240_FLASH_READ				0x01
#define AR7240_FLASH_WRITE				0x02
#define AR7240_FLASH_ERASE				0x03


#define AR7240_IO_MAGIC 				0xB3
#define	AR7240_IO_FLASH_READ			_IOR(AR7240_IO_MAGIC, AR7240_FLASH_READ, char)
#define AR7240_IO_FLASH_WRITE			_IOW(AR7240_IO_MAGIC, AR7240_FLASH_WRITE, char)
#define	AR7240_IO_FLASH_ERASE			_IO (AR7240_IO_MAGIC, AR7240_FLASH_ERASE)

#define	AR7240_IOC_MAXNR				14
#define flash_major      				239
#define flash_minor      				0

extern void update_wdt(void);

int ar7240_flash_ioctl(struct inode *inode, struct file *file,  unsigned int cmd, unsigned long arg);
int ar7240_flash_open (struct inode *inode, struct file *file);

struct file_operations flash_device_op = {
        .owner = THIS_MODULE,
        .ioctl = ar7240_flash_ioctl,
        .open = ar7240_flash_open,
};

static struct cdev flash_device_cdev = {
        .owner  = THIS_MODULE,
		.ops = &flash_device_op,
};

typedef struct 
{
	u_int32_t addr;		/* flash r/w addr	*/
	u_int32_t len;		/* r/w length		*/
	u_int8_t* buf;		/* user-space buffer*/
	u_int32_t buflen;	/* buffer length	*/
	u_int32_t reset;	/* reset flag 		*/
}ARG;

#define AR7240_FLASH_SECTOR_SIZE	(64 * 1024)

struct mtd_info *mtd_info_buf = NULL;
unsigned char *flash_sector_buf = NULL;
	
int ar7240_flash_ioctl(struct inode *inode, struct file *file,  unsigned int cmd, unsigned long arg)
{
	ARG *pArg = (ARG*)arg;
	u_int8_t* usrBuf = pArg->buf;
	u_int32_t usrBufLen = pArg->buflen;
	u_int32_t addr = pArg->addr;
	u_int32_t reset = pArg->reset;
	
	int i;
	int ret = 0;
	size_t retlen;
	int addr_start, addr_end;
	
	int nSector = usrBufLen >> 16; 			/* Divide AR7240_FLASH_SECTOR_SIZE */	
	int oddLen = usrBufLen & 0x0000FFFF;	/* odd length (0 ~ AR7240_FLASH_SECTOR_SIZE) */

	if ((mtd_info_buf == NULL) || (flash_sector_buf == NULL))
	{
		goto wrong;
	}
	
	memset(mtd_info_buf, 0, sizeof(struct mtd_info));

	mtd_info_buf->size               =   __ath_flash_size * 1024 * 1024;
	mtd_info_buf->erasesize          =   AR7240_FLASH_SECTOR_SIZE;
	mtd_info_buf->numeraseregions    =   0;
	mtd_info_buf->eraseregions       =   NULL;
	mtd_info_buf->owner              =   THIS_MODULE;

	if (_IOC_TYPE(cmd) != AR7240_IO_MAGIC)
	{
		printk("cmd type error!\n");
		goto wrong;
	}
	if (_IOC_NR(cmd) > AR7240_IOC_MAXNR)
	{
		printk("cmd NR error!\n");
		goto wrong;
	}
	if (_IOC_DIR(cmd) & _IOC_READ)
	{
		ret = access_ok(VERIFY_WRITE, (void __user *)usrBuf, _IOC_SIZE(cmd));
	}
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
	{
		ret = access_ok(VERIFY_READ, (void __user *)usrBuf, _IOC_SIZE(cmd));
	}
	if (ret < 0)
	{ 
		printk("access no ok!\n");
		goto wrong;
	}

	switch(cmd)
	{
		case AR7240_IO_FLASH_READ:
		{
		memset(flash_sector_buf, 0, AR7240_FLASH_SECTOR_SIZE);
		
		ret = ath_flash_read(mtd_info_buf, addr, usrBufLen, &retlen, flash_sector_buf);
			if ((ret == 0) && (retlen == usrBufLen))
			{
				/*copy from kernel to user*/
			if (copy_to_user(usrBuf, flash_sector_buf, usrBufLen))
				{
					printk("read copy_to_user failed\n");
					goto wrong;
				}
			}
			else
			{
				printk("Read failed, ret:%d from:%d size:%d\n", ret, addr, usrBufLen);
				goto wrong;
			}
			goto good;
			break;
		}
		
		case AR7240_IO_FLASH_WRITE:
		{
			addr_start	= addr & 0xFFFF0000;			/* first sector addr to erase */
            addr_end	= (addr + usrBufLen) & 0xFFFF0000;	/* last sector addr to erase */
			
			printk("Erase from %#X to %#X:", addr, addr + usrBufLen);
            do
			{
            	ath_spi_sector_erase(addr_start);
				printk(".");
				addr_start += AR7240_FLASH_SECTOR_SIZE;
            }while (addr_start < addr_end);

			printk("\n");
			
            ath_spi_done();
			printk("Program from %#X to %#X:", addr, addr + usrBufLen);
			for (i = 0; i < nSector; i++)		
			{
			memset(flash_sector_buf, 0, AR7240_FLASH_SECTOR_SIZE);
			
			if (copy_from_user(flash_sector_buf, usrBuf, AR7240_FLASH_SECTOR_SIZE))
				{
					printk("config write copy_from_usr failed!\n");
					goto wrong;				
				}
			
			ret =  ath_flash_write(mtd_info_buf, addr, AR7240_FLASH_SECTOR_SIZE, &retlen, flash_sector_buf);
				printk(".");
				if ((ret != 0) && (retlen != AR7240_FLASH_SECTOR_SIZE))
				{
					printk("\nWrite to flash failed status:%d retlen:%d\n", ret, retlen);
                	goto wrong;
				}
				addr += AR7240_FLASH_SECTOR_SIZE;
				usrBuf += AR7240_FLASH_SECTOR_SIZE;
			}

			if (oddLen)
			{
			memset(flash_sector_buf, 0, AR7240_FLASH_SECTOR_SIZE);
			
			if (copy_from_user(flash_sector_buf, usrBuf, oddLen))
				{
	                printk("config write copy_from_usr failed!\n");
	                goto wrong;
	            }
			ret = ath_flash_write(mtd_info_buf, addr, oddLen, &retlen, flash_sector_buf);

	            if ((ret != 0) && (retlen != oddLen))
				{
	                printk("\nWrite to flash failed status:%d retlen:%d\n", ret, retlen);
	                goto wrong;
	            }
			}
			printk("\nwrite successfully\n");
			
			if (reset)
			{
			/* 
			 * reboot from userspace 
			 * but somtimes unlzma kernel thread will occur some errors
			 * so we use this hardware watchdog timer to make sure reboot
			 * will success
			 * this is a workaround, may be should stop unlzma first
			 */
			update_wdt();
				printk("reboot...\n");
			}
			
            goto good;
			break;
		}
		
		case  AR7240_FLASH_ERASE:
		{
			goto good;
			break;
		}
	}


good:
	return 0;
wrong:
	return -1;
	
}


int ar7240_flash_open (struct inode *inode, struct file *filp)
{
	int minor = iminor(inode);
	int devnum = minor;
	struct mtd_info *mtd;
	
	if ((filp->f_mode & 2) && (minor & 1)) {
		printk("You can't open the RO devices RW!\n");
		return -EACCES;
	}

	mtd = get_mtd_device(NULL, devnum);   
	if (!mtd) {
		printk("Can not open mtd!\n");
		return -ENODEV;	
	}
	filp->private_data = mtd;
	return 0;
	
}

int __init ar7240_flash_chrdev_init (void)
{
    dev_t dev;
    int result;
    int err;
    int ar7240_flash_major = flash_major;
    int ar7240_flash_minor = flash_minor;

    if (ar7240_flash_major) {
        dev = MKDEV(ar7240_flash_major, ar7240_flash_minor);
        result = register_chrdev_region(dev, 1, "ar7240_flash_chrdev");
    } else {
        result = alloc_chrdev_region(&dev, ar7240_flash_minor, 1, "ar7240_flash_chrdev");
        ar7240_flash_major = MAJOR(dev);
    }

    if (result < 0) {
        printk(KERN_WARNING "ar7240_flash_chrdev : can`t get major %d\n", ar7240_flash_major);
        return result;
    }
    cdev_init (&flash_device_cdev, &flash_device_op);
    err = cdev_add(&flash_device_cdev, dev, 1);
    if (err)
		printk(KERN_NOTICE "Error %d adding flash_chrdev ", err);

	/*
	 * alloc buf at init, only need alloc once
	 * if in ioctl func, may be failed
	 */
	mtd_info_buf = (struct mtd_info *)kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (mtd_info_buf)
	{
		flash_sector_buf = (unsigned char *)kmalloc(AR7240_FLASH_SECTOR_SIZE, GFP_KERNEL);

		if (NULL == flash_sector_buf)
		{
			kfree(mtd_info_buf);
			printk("%s kmalloc flash sector buf failed\n", __func__);
		}
	}
	else
	{
		printk("%s kmalloc mtd info buf failed\n", __func__);
	}
	
    return 0;

}

static void __exit cleanup_ar7240_flash_chrdev_exit (void)
{
	if (mtd_info_buf)
	{
		kfree(mtd_info_buf);
	}

	if (flash_sector_buf)
	{
		kfree(flash_sector_buf);
	}
}


module_init(ar7240_flash_chrdev_init);
module_exit(cleanup_ar7240_flash_chrdev_exit);

