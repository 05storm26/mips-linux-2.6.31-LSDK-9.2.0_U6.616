/*
 * Parse RedBoot-style Flash Image System (FIS) tables and
 * produce a Linux partition array to match.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/vmalloc.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#define LINUX_TAG_OFFSET		0x20000

#define TAG_LEN         512
#define SIG_LEN         24
#define SIG_LEN_2       12   // Original second SIG = 20 is now devided into 14 for SIG_LEN_2 and 6 for CHIP_ID
#define CHIP_ID_LEN		8	
#define TOKEN_LEN       20
#define BOARD_ID_LEN    16// TAG for downloadable image (kernel plus file system)// integer in the structure is stored in Network-Byte order (BIG-endian)


typedef struct _LINUX_FILE_TAG
{	
	unsigned long tagVersion;    
	char 		  signiture_1[SIG_LEN];         // text line for company info	
	char 		  signiture_2[SIG_LEN_2];       // additional info (can be version number)    
	char 		  chipId[CHIP_ID_LEN];			// chip id     
	char 		  boardId[BOARD_ID_LEN];        // board id    
	unsigned long productId;					// product id    
	unsigned long productVer;					// product version    
	unsigned long reserved1;					// reserved for future
	
	unsigned char imageValidationToken[TOKEN_LEN];// image validation token - md5 checksum
	unsigned char kernelValidationToken[TOKEN_LEN];	// kernel+tag validation token - md5 checksum
	
	unsigned long kernelTextAddr;				// text section address of kernel	
	unsigned long kernelEntryPoint;				// entry point address of kernel		
	
	unsigned long totalImageLen;				// the sum of kernelLen+rootfsLen+tagLen
	
	unsigned long kernelAddress;				// starting address (offset from the beginning of FILE_TAG) of kernel image	
	unsigned long kernelLen;					// length of kernel image
	
	unsigned long rootfsAddress;				// starting address (offset) of filesystem image	
	unsigned long rootfsLen;					// length of filesystem image
	
	unsigned long bootloaderAddress;			// starting address (offset) of boot loader image	
	unsigned long bootloaderLen;				// length of boot loader image
	
} LINUX_FILE_TAG;


struct fis_image_desc {
    unsigned char name[16];      // Null terminated name
    uint32_t	  flash_base;    // Address within FLASH of image
    uint32_t	  mem_base;      // Address in memory where it executes
    uint32_t	  size;          // Length of image
    uint32_t	  entry_point;   // Execution entry point
    uint32_t	  data_length;   // Length of actual data
    unsigned char _pad[256-(16+7*sizeof(uint32_t))];
    uint32_t	  desc_cksum;    // Checksum over image descriptor
    uint32_t	  file_cksum;    // Checksum over image data
};

/********add by zhangwu 24May07***************/
struct fis_image_desc flash_sectors[] = {
	{
		"boot",//name
		0x0,	 //flash_base
		0x0,	 //mem_base
		0x20000, //size 
		0x0,	//entry_point
	},
	{
		"kernel",
		0x20000,
		0x80060000,
		0x100000,
		0x8025c000,
		0x200000,
	},
	{
		"rootfs",
		0x120000,	//0x140000,
		0x80400000,
		0x6C0000,	//0x2a0000,
		0x0,
	},

	{
		"config",
		0x3e0000,
		0x80800000,
		0x20000,
		0x0,
	},
	{
		"art",
		0x3e0000,
		0x80800000,
		0x20000,
		0x0,
	},
	{
		0xff,
	}
};



struct fis_list {
	struct fis_image_desc *img;
	struct fis_list *next;
};

static int directory = CONFIG_MTD_REDBOOT_DIRECTORY_BLOCK;
module_param(directory, int, 0);

static inline int redboot_checksum(struct fis_image_desc *img)
{
	/* RedBoot doesn't actually write the desc_cksum field yet AFAICT */
	return 1;
}

static int parse_redboot_partitions(struct mtd_info *master,
                             struct mtd_partition **pparts,
                             unsigned long fis_origin)
{
	int nrparts = 0;
	struct fis_image_desc *buf;
	struct mtd_partition *parts;
	struct fis_list *fl = NULL, *tmp_fl;
	int ret, i;
	size_t retlen;
	char *names;
	char *nullname;
	int namelen = 0;
	int nulllen = 0;
	int numslots;
	unsigned long offset;
#ifdef CONFIG_MTD_REDBOOT_PARTS_UNALLOCATED
	static char nullstring[] = "unallocated";
#endif

#if 0/**********del by zhangwu 24May07*************/
	if ( directory < 0 ) {
		offset = master->size + directory * master->erasesize;
		while (master->block_isbad && 
		       master->block_isbad(master, offset)) {
			if (!offset) {
			nogood:
				printk(KERN_NOTICE "Failed to find a non-bad block to check for RedBoot partition table\n");
				return -EIO;
			}
			offset -= master->erasesize;
		}
	} else {
		offset = directory * master->erasesize;
		while (master->block_isbad && 
		       master->block_isbad(master, offset)) {
			offset += master->erasesize;
			if (offset == master->size)
				goto nogood;
		}
	}
	buf = vmalloc(master->erasesize);

	if (!buf)
		return -ENOMEM;

	printk(KERN_NOTICE "Searching for RedBoot partition table in %s at offset 0x%lx\n",
	       master->name, offset);

	ret = master->read(master, offset,
			   master->erasesize, &retlen, (void *)buf);

	if (ret)
		goto out;

	if (retlen != master->erasesize) {
		ret = -EIO;
		goto out;
	}
#endif
	LINUX_FILE_TAG tag;
	/* Read mtd partion from flash tag, add by lsz 30Nov07 */
	ret = master->read(master, LINUX_TAG_OFFSET, sizeof(tag), &retlen, (void *)&tag);
	for (i = 0; i < sizeof(flash_sectors)/sizeof(struct fis_image_desc); i ++)
	{
		if (strcmp(flash_sectors[i].name, "rootfs") == 0)
		{
			flash_sectors[i].flash_base = LINUX_TAG_OFFSET + tag.rootfsAddress;
			flash_sectors[i].size = tag.rootfsLen;
		}
		else if (strcmp(flash_sectors[i].name, "config") == 0)
		{
			flash_sectors[i].flash_base = LINUX_TAG_OFFSET + tag.totalImageLen;
			flash_sectors[i].size = 0x10000;	/* config size */
		}
		else if (strcmp(flash_sectors[i].name, "art") == 0)
		{
			/* art is the last sector, flash size is 8MB aligned -- lsz, 081211 */
			flash_sectors[i].flash_base = ((tag.totalImageLen + 0x7FFFFF) & 0xFFC00000)-0x10000;
			flash_sectors[i].size = 0x10000;	/* config size */
		}
	}
	/************add by zhangwu 24May07********************/
	buf = flash_sectors;
	printk(KERN_INFO "Searching for RedBoot partition table\n");
	/******************************************************/

	numslots = (master->erasesize / sizeof(struct fis_image_desc));
	
	#if 0	/* del by lsz 081211 */
	for (i = 0; i < numslots; i++) {
		if (!memcmp(buf[i].name, "FIS directory", 14)) {
			/* This is apparently the FIS directory entry for the
			 * FIS directory itself.  The FIS directory size is
			 * one erase block; if the buf[i].size field is
			 * swab32(erasesize) then we know we are looking at
			 * a byte swapped FIS directory - swap all the entries!
			 * (NOTE: this is 'size' not 'data_length'; size is
			 * the full size of the entry.)
			 */

			/* RedBoot can combine the FIS directory and
			   config partitions into a single eraseblock;
			   we assume wrong-endian if either the swapped
			   'size' matches the eraseblock size precisely,
			   or if the swapped size actually fits in an
			   eraseblock while the unswapped size doesn't. */
			if (swab32(buf[i].size) == master->erasesize ||
			    (buf[i].size > master->erasesize
			     && swab32(buf[i].size) < master->erasesize)) {
				int j;
				/* Update numslots based on actual FIS directory size */
				numslots = swab32(buf[i].size) / sizeof (struct fis_image_desc);
				for (j = 0; j < numslots; ++j) {

					/* A single 0xff denotes a deleted entry.
					 * Two of them in a row is the end of the table.
					 */
					if (buf[j].name[0] == 0xff) {
				  		if (buf[j].name[1] == 0xff) {
							break;
						} else {
							continue;
						}
					}

					/* The unsigned long fields were written with the
					 * wrong byte sex, name and pad have no byte sex.
					 */
					swab32s(&buf[j].flash_base);
					swab32s(&buf[j].mem_base);
					swab32s(&buf[j].size);
					swab32s(&buf[j].entry_point);
					swab32s(&buf[j].data_length);
					swab32s(&buf[j].desc_cksum);
					swab32s(&buf[j].file_cksum);
				}
			} else if (buf[i].size < master->erasesize) {
				/* Update numslots based on actual FIS directory size */
				numslots = buf[i].size / sizeof(struct fis_image_desc);
			}
			break;
		}
	}
	if (i == numslots) {
		/* Didn't find it */
		printk(KERN_NOTICE "No RedBoot partition table detected in %s\n",
		       master->name);
		ret = 0;
		goto out;
	}
	#endif 

	for (i = 0; i < numslots; i++) {
		struct fis_list *new_fl, **prev;

		if (buf[i].name[0] == 0xff)
				break;
		if (!redboot_checksum(&buf[i]))
			break;

		new_fl = kmalloc(sizeof(struct fis_list), GFP_KERNEL);
		namelen += strlen(buf[i].name)+1;
		if (!new_fl) {
			ret = -ENOMEM;
			goto out;
		}
		new_fl->img = &buf[i];
                if (fis_origin) {
                        buf[i].flash_base -= fis_origin;
                } else {
                        buf[i].flash_base &= master->size-1;
                }

		/* I'm sure the JFFS2 code has done me permanent damage.
		 * I now think the following is _normal_
		 */
		prev = &fl;
		while(*prev && (*prev)->img->flash_base < new_fl->img->flash_base)
			prev = &(*prev)->next;
		new_fl->next = *prev;
		*prev = new_fl;

		nrparts++;
	}
#ifdef CONFIG_MTD_REDBOOT_PARTS_UNALLOCATED
	if (fl->img->flash_base) {
		nrparts++;
		nulllen = sizeof(nullstring);
	}

	for (tmp_fl = fl; tmp_fl->next; tmp_fl = tmp_fl->next) {
		if (tmp_fl->img->flash_base + tmp_fl->img->size + master->erasesize <= tmp_fl->next->img->flash_base) {
			nrparts++;
			nulllen = sizeof(nullstring);
		}
	}
#endif
	parts = kzalloc(sizeof(*parts)*nrparts + nulllen + namelen, GFP_KERNEL);

	if (!parts) {
		ret = -ENOMEM;
		printk("Malloc parts failed\n");
		goto out;
	}

	memset(parts, 0, sizeof(*parts)*nrparts + nulllen + namelen);

	nullname = (char *)&parts[nrparts];
#ifdef CONFIG_MTD_REDBOOT_PARTS_UNALLOCATED
	if (nulllen > 0) {
		strcpy(nullname, nullstring);
	}
#endif
	names = nullname + nulllen;

	i=0;

#ifdef CONFIG_MTD_REDBOOT_PARTS_UNALLOCATED
	if (fl->img->flash_base) {
	       parts[0].name = nullname;
	       parts[0].size = fl->img->flash_base;
	       parts[0].offset = 0;
		i++;
	}
#endif
	for ( ; i<nrparts; i++) {
		parts[i].size = fl->img->size;
		parts[i].offset = fl->img->flash_base;
		parts[i].name = names;

		strcpy(names, fl->img->name);
#ifdef CONFIG_MTD_REDBOOT_PARTS_READONLY
		if (!memcmp(names, "RedBoot", 8) ||
				!memcmp(names, "RedBoot config", 15) ||
				!memcmp(names, "FIS directory", 14)) {
			parts[i].mask_flags = MTD_WRITEABLE;
		}
#endif
		names += strlen(names)+1;

#ifdef CONFIG_MTD_REDBOOT_PARTS_UNALLOCATED
		if(fl->next && fl->img->flash_base + fl->img->size + master->erasesize <= fl->next->img->flash_base) {
			i++;
			parts[i].offset = parts[i-1].size + parts[i-1].offset;
			parts[i].size = fl->next->img->flash_base - parts[i].offset;
			parts[i].name = nullname;
		}
#endif
		tmp_fl = fl;
		fl = fl->next;
		kfree(tmp_fl);
	}
	ret = nrparts;
	*pparts = parts;
 out:
	while (fl) {
		struct fis_list *old = fl;
		fl = fl->next;
		kfree(old);
	}
    /* this is a bug ,modified by liaoxinkai 2010.6.2 */
	//vfree(buf);
	return ret;
}

static struct mtd_part_parser redboot_parser = {
	.owner = THIS_MODULE,
	.parse_fn = parse_redboot_partitions,
	.name = "RedBoot",
};

static int __init redboot_parser_init(void)
{
	return register_mtd_parser(&redboot_parser);
}

static void __exit redboot_parser_exit(void)
{
	deregister_mtd_parser(&redboot_parser);
}

module_init(redboot_parser_init);
module_exit(redboot_parser_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Parsing code for RedBoot Flash Image System (FIS) tables");
