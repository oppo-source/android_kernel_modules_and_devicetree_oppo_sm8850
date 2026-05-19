/*===============================================================================
** Copyright (C), 2008-2025, OPLUS Mobile Comm Corp., Ltd
** OPLUS_FEATURE_SENSOR_DRIVER
** File: data_record_driver.c
**
** Description:
**      Definitions for Data log driver.
**
** Function:
** Provide mmap interface to map shared memory to user space
**
** Version: 1.0
** Date created: 2025/11/28
**
** --------------------------- Revision History: ------------------------------------
* <version>        <date>               <author>               <desc>
===============================================================================*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/io.h>
#include <asm/io.h>
#include <linux/pfn.h>
#include <asm/page.h>
#include <linux/proc_fs.h>
#include <linux/soc/qcom/smem.h>  // Add: qcom_smem related API
#include <linux/vmalloc.h>
#include <linux/mutex.h>

#define DRV_TAG                             "<data_record>"
#define DBG(fmt, ...)                       pr_debug(DRV_TAG fmt, ##__VA_ARGS__);
#define INF(fmt, ...)                       pr_info(DRV_TAG fmt, ##__VA_ARGS__);
#define ERR(fmt, ...)                       pr_err(DRV_TAG fmt, ##__VA_ARGS__);

#define DEVICE_NAME "data_record"

/*==============================================================================
  Type Definitions
============================================================================*/
typedef struct sensor_data_t {
    int64_t timestamp;
    int32_t sensor_type;
    int32_t    x;
    int32_t    y;
    int32_t    z;
} __attribute__((packed)) sensor_data_t;

#define OPLUS_DATA_RECORD_MEM_ID         490

#define SENSOR_DATA_SIZE        (sizeof(sensor_data_t))

/* Size of shared memory */
#define ISLAND_MAX_RECORDS      1000
#define ISLAND_BUFFER_SIZE      (sizeof(sensor_data_t) * ISLAND_MAX_RECORDS)  // 24KB
#define DATA_LOG_SHARED_MEM_META_SIZE    (24)           // Metadata size
#define DATA_LOG_SHARED_MEM_MAX_ISLAND_COUNT (5)       // Maximum number of Island buffers
#define DATA_LOG_SHARED_MEM_DATA_SIZE    (ISLAND_BUFFER_SIZE * DATA_LOG_SHARED_MEM_MAX_ISLAND_COUNT) // 120KB
#define DATA_LOG_SHARED_MEM_SIZE         (DATA_LOG_SHARED_MEM_DATA_SIZE + DATA_LOG_SHARED_MEM_META_SIZE)  // 120KB

/* Shared memory ring buffer structure */
typedef struct {
    volatile uint32_t read_index;   // Read index (maintained by AP)
    volatile uint32_t write_index;  // Write index (maintained by ADSP)
    volatile uint32_t buffer_size;  // Buffer size
    volatile uint32_t data_count;   // Current number of data records
    volatile uint32_t sync_flag;    // AP and adsp sync flag
    volatile uint32_t start_flag;   // start flag, 0: stopped, 1: started
    uint8_t           data[];       // Actual data area
} __attribute__((packed))  data_log_shared_mem_ring_buffer_t;

/* Driver private data */
struct data_log_driver_data_t {
    void *shared_mem_base;             // Base address of shared memory (virtual address)
    phys_addr_t shared_mem_phys;       // Physical address of shared memory (used for mmap zero-copy)
    size_t shared_mem_size;            // Size of shared memory
    void *map_buffer;                  // Buffer for mmap mapping to userspace
    size_t map_buffer_size;            // Size of map buffer
    struct mutex map_mutex;            // Mutex to protect map_buffer access
    dev_t dev_num;                     // Device number
    bool is_initialized;               // Flag to check if share memory is initialized
    bool start_flag;                   // Flag to check if data record is started

    struct cdev *sensor_cdev;
    struct class *sensor_class;
    struct device *sensor_device;
    struct proc_dir_entry *data_record_proc_entry;
};

static struct data_log_driver_data_t *g_driver_data = NULL;

static int data_record_get_smem(void **vmem, size_t *size)
{
	void *smem_virt;
	size_t smem_size = DATA_LOG_SHARED_MEM_SIZE;
	// 2 is for adsp
	smem_virt = qcom_smem_get(2, OPLUS_DATA_RECORD_MEM_ID, &smem_size);
	if (IS_ERR(smem_virt)) {
		ERR("qcom_smem_get failed, host: 2 type: %d size: %ldB\n",
			OPLUS_DATA_RECORD_MEM_ID, smem_size);
		return -ENOMEM;
	}
	if (vmem) {
		*vmem = smem_virt;
	}
	if (size) {
		*size = smem_size;
	}
	INF("qcom_smem_get succeeded, host: 2 type: %d size: %ldB\n",
		OPLUS_DATA_RECORD_MEM_ID, smem_size);
	return 0;
}

static int data_record_init_share_memory(struct data_log_driver_data_t *pdriver_data)
{
    int ret = 0;
    void *shared_mem_virt;
    size_t shared_mem_size;
    phys_addr_t shared_mem_phys;

    if (pdriver_data->is_initialized) {
        INF("sensor_data: share memory is already initialized\n");
        return 0;
    }

    /* Get shared memory (through qcom_smem_get) */
    ret = data_record_get_smem(&shared_mem_virt, &shared_mem_size);
    if (ret == 0) {
        pdriver_data->shared_mem_base = shared_mem_virt;
        pdriver_data->shared_mem_size = shared_mem_size;

        /* Get physical address of shared memory (used for mmap zero-copy) */
        /* For memory obtained through qcom_smem_get, special handling is required */
        /* Method 1: If smem region is contiguous physical memory, use virt_to_phys */
        /* Method 2: Use page_to_pfn and pfn_to_phys */
        /* Note: Memory from qcom_smem may not be directly mapped, adjust according to the actual situation */
        shared_mem_phys = qcom_smem_virt_to_phys(shared_mem_virt);
        if (shared_mem_phys != 0) {
            INF("shared memory: virt=0x%p, phys=0x%llx, size=%zu\n",
                shared_mem_virt, shared_mem_phys, shared_mem_size);

            /* Allocate map_buffer for userspace mapping */
            pdriver_data->map_buffer_size = PAGE_ALIGN(shared_mem_size);
            pdriver_data->map_buffer = vmalloc(pdriver_data->map_buffer_size);
            if (!pdriver_data->map_buffer) {
                ERR("failed to allocate map_buffer, size=%zu\n", pdriver_data->map_buffer_size);
                ret = -ENOMEM;
                return ret;
            }

            /* Initialize mutex for map_buffer protection */
            mutex_init(&pdriver_data->map_mutex);

            /* Copy initial data from shared_mem_base to map_buffer */
            memcpy(pdriver_data->map_buffer, pdriver_data->shared_mem_base, shared_mem_size);

            INF("map_buffer allocated: virt=0x%p, size=%zu\n",
                pdriver_data->map_buffer, pdriver_data->map_buffer_size);

            pdriver_data->is_initialized = true;
            pdriver_data->shared_mem_phys = shared_mem_phys;
        } else {
            ret = -ENOMEM;
        }
    }

    return ret;
}

/**
 * @brief Device open function
 */
static int data_record_device_open(struct inode *inode, struct file *file)
{
    if (data_record_init_share_memory(g_driver_data) != 0) {
        INF("sensor_data: device failed to open, share memory init fail.\n");
        return -ENOMEM;
    }
    INF("sensor_data: device opened\n");
    return 0;
}

/**
 * @brief Device release function
 */
static int data_record_device_release(struct inode *inode, struct file *file)
{
    INF("sensor_data: device closed\n");
    return 0;
}

/**
 * @brief mmap handler function - map map_buffer to userspace
 * Copy data from shared_mem_base to map_buffer, then map map_buffer to user space
 */
static int data_record_device_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long start = vma->vm_start;
    struct page *page;
    unsigned long pfn;
    int ret;
    void *map_buffer_virt;
    unsigned long i;

    /* Check if g_driver_data is valid */
    if (!g_driver_data) {
        ERR("mmap: g_driver_data is NULL\n");
        return -ENODEV;
    }

    /* Ensure shared memory is initialized */
    if (!g_driver_data->is_initialized) {
        ret = data_record_init_share_memory(g_driver_data);
        if (ret != 0) {
            ERR("mmap: failed to initialize shared memory\n");
            return ret;
        }
    }

    /* Check if map_buffer is allocated */
    if (!g_driver_data->map_buffer) {
        ERR("mmap: map_buffer is NULL\n");
        return -ENODEV;
    }

    /* Check mapping size and offset */
    if (offset >= g_driver_data->map_buffer_size) {
        ERR("mmap offset too large: 0x%lx >= 0x%zx\n", offset, g_driver_data->map_buffer_size);
        return -EINVAL;
    }

    if (offset + size > g_driver_data->map_buffer_size) {
        ERR("mmap size exceeds buffer: 0x%lx > 0x%zx\n",
            offset + size, g_driver_data->map_buffer_size);
        return -EINVAL;
    }

    /* Copy latest data from shared_mem_base to map_buffer before mapping */
    mutex_lock(&g_driver_data->map_mutex);
    if (g_driver_data->shared_mem_base && g_driver_data->shared_mem_size > 0) {
        data_log_shared_mem_ring_buffer_t* rb = NULL;
        size_t copy_size = (size < (g_driver_data->shared_mem_size - offset)) ?
                           size : (g_driver_data->shared_mem_size - offset);
        if (copy_size > 0) {
            memcpy((char *)g_driver_data->map_buffer + offset,
                   (char *)g_driver_data->shared_mem_base + offset, copy_size);
        }
        rb = (data_log_shared_mem_ring_buffer_t*)g_driver_data->shared_mem_base;
        if (rb != NULL && rb->sync_flag == 0) {
            rb->sync_flag = 1;
            rb->read_index = rb->write_index;
            rb->data_count = 0;
            rb->sync_flag = 0;
        }
    }
    mutex_unlock(&g_driver_data->map_mutex);

    /* Get virtual address of map_buffer at offset */
    map_buffer_virt = (char *)g_driver_data->map_buffer + offset;

    /* Set VMA attributes */
    vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
    /* Use cached mapping for better performance */
    vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

    /* Map vmalloc memory page by page */
    for (i = 0; i < size; i += PAGE_SIZE) {
        unsigned long page_offset = offset + i;
        unsigned long user_addr = start + i;
        unsigned long page_size = (size - i < PAGE_SIZE) ? (size - i) : PAGE_SIZE;

        /* Get page from vmalloc address */
        page = vmalloc_to_page((char *)map_buffer_virt + i);
        if (!page) {
            ERR("vmalloc_to_page failed at offset 0x%lx\n", page_offset);
            ret = -EINVAL;
            goto err_out;
        }

        pfn = page_to_pfn(page);

        /* Map this page to user space */
        ret = remap_pfn_range(vma, user_addr, pfn, page_size, vma->vm_page_prot);
        if (ret) {
            ERR("remap_pfn_range failed: %d (pfn=0x%lx, size=0x%lx, addr=0x%lx)\n",
                ret, pfn, page_size, user_addr);
            goto err_out;
        }
    }

    INF("mmap success: map_buffer=0x%p, offset=0x%lx -> virt=0x%lx, size=0x%lx\n",
        map_buffer_virt, offset, vma->vm_start, size);
    return 0;

err_out:
    return ret;
}

/* Device file operations structure */
static const struct file_operations sensor_fops = {
    .owner = THIS_MODULE,
    .open = data_record_device_open,
    .release = data_record_device_release,
    .mmap = data_record_device_mmap,
};

static ssize_t data_record_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct data_log_driver_data_t *pdriver_data = g_driver_data;
    /* Check if pdriver_data is valid */
    if (!pdriver_data || !buf) {
        ERR("data_record_show: pdriver_data or buf is NULL\n");
        return -ENODEV;
    }
    data_log_shared_mem_ring_buffer_t* rb = (data_log_shared_mem_ring_buffer_t*)pdriver_data->shared_mem_base;
    if (rb == NULL) {
        ERR("data_record_show: rb is NULL\n");
        return 0;
    }
    return sprintf(buf, "start_flag->%d\n"
                        "ap_base:  0x%p\n"
                        "shm_size: 0x%lu\n"
                        "buffer_size: %u\n"
                        "read_pos: %u\n"
                        "write_pos:%u\n"
                        "count:    %u\n"
                        "sync_flag: %u\n",
                        rb->start_flag, pdriver_data->shared_mem_base,
                        pdriver_data->shared_mem_size, rb->buffer_size, rb->read_index,
                        rb->write_index, rb->data_count, rb->sync_flag);
}

static ssize_t data_record_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct data_log_driver_data_t *pdriver_data = g_driver_data;
    /* Check if pdriver_data is valid */
    if (!pdriver_data) {
        ERR("mmap: pdriver_data is NULL\n");
        return -ENODEV;
    }
	if (!(g_driver_data->is_initialized)) {
		if(data_record_init_share_memory(pdriver_data) != 0) {
            ERR("data_record_store: failed to initialize shared memory\n");
            return -EINVAL;
        }
    }
    if (strncmp(buf, "1", 1) == 0) {
        INF("%s:echo 1\n", __func__);
        pdriver_data->start_flag = true;
    } else if (strncmp(buf, "0", 1) == 0) {
        INF("%s:echo 0\n", __func__);
        pdriver_data->start_flag = false;
    } else {
        ERR("%s:invalid value\n", __func__);
        return -EINVAL;
    }

    data_log_shared_mem_ring_buffer_t* rb = (data_log_shared_mem_ring_buffer_t*)pdriver_data->shared_mem_base;
    if (rb != NULL) {
        rb->start_flag = pdriver_data->start_flag ? 1 : 0;
    }
    return count;
}

DEVICE_ATTR(data_record, 0660, data_record_show, data_record_store);

static struct attribute *data_record_attributes[] = {
    &dev_attr_data_record.attr,
    NULL
};

static struct attribute_group data_record_attribute_group = {
    .attrs = data_record_attributes
};

/**
 * @brief Driver initialization function
 */
static int __init data_record_driver_init(void)
{
    int ret;

    INF("driver init\n");
    /* Allocate driver private data */
    g_driver_data = kzalloc(sizeof(struct data_log_driver_data_t), GFP_KERNEL);
    if (!g_driver_data) {
        ERR("failed to allocate driver data\n");
        return -ENOMEM;
    }

    g_driver_data->is_initialized = false;
    g_driver_data->map_buffer = NULL;
    g_driver_data->map_buffer_size = 0;

    data_record_init_share_memory(g_driver_data);

    /* Allocate device number */
    ret = alloc_chrdev_region(&(g_driver_data->dev_num), 0, 1, DEVICE_NAME);
    if (ret < 0) {
        ERR("failed to allocate chrdev region\n");
        goto err_free_driver_data;
    }

    /* Initialize character device */
    g_driver_data->sensor_cdev = cdev_alloc();
    if (!(g_driver_data->sensor_cdev)) {
        ERR("failed to allocate cdev\n");
        ret = -ENOMEM;
        goto err_unregister_chrdev;
    }

    cdev_init(g_driver_data->sensor_cdev, &sensor_fops);
    g_driver_data->sensor_cdev->owner = THIS_MODULE;

    /* Add character device */
    ret = cdev_add(g_driver_data->sensor_cdev, g_driver_data->dev_num, 1);
    if (ret < 0) {
        ERR("failed to add cdev\n");
        goto err_del_cdev;
    }

    /* Create device class */
    g_driver_data->sensor_class = class_create(DEVICE_NAME);
    if (IS_ERR(g_driver_data->sensor_class)) {
        ERR("failed to create class\n");
        ret = PTR_ERR(g_driver_data->sensor_class);
        goto err_del_cdev;
    }

    /* Create device node */
    g_driver_data->sensor_device = device_create(g_driver_data->sensor_class, NULL, g_driver_data->dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(g_driver_data->sensor_device)) {
        ERR("failed to create device\n");
        ret = PTR_ERR(g_driver_data->sensor_device);
        goto err_destroy_class;
    }

    /* Create sysfs attribute group */
    ret = sysfs_create_group(&g_driver_data->sensor_device->kobj, &data_record_attribute_group);
    if (ret < 0) {
        ERR("failed to create sysfs attribute group\n");
        goto err_destroy_device;
    }

    INF("device node: %s (major=%d, minor=%d)\n",
        DEVICE_NAME, MAJOR(g_driver_data->dev_num), MINOR(g_driver_data->dev_num));

    return 0;

err_destroy_device:
    device_destroy(g_driver_data->sensor_class, g_driver_data->dev_num);
err_destroy_class:
    class_destroy(g_driver_data->sensor_class);
err_del_cdev:
    cdev_del(g_driver_data->sensor_cdev);
err_unregister_chrdev:
    unregister_chrdev_region(g_driver_data->dev_num, 1);
err_free_driver_data:
    kfree(g_driver_data);
    return ret;
}

/**
 * @brief Driver exit function
 */
static void __exit data_record_driver_exit(void)
{
    pr_info("sensor_data: driver exit\n");

    if (!g_driver_data) {
        ERR("driver data is NULL\n");
        return;
    }

    /* Remove sysfs attribute group */
    if (g_driver_data->sensor_device) {
        sysfs_remove_group(&g_driver_data->sensor_device->kobj, &data_record_attribute_group);
    }

    /* Remove device node */
    if (g_driver_data->sensor_device) {
        device_destroy(g_driver_data->sensor_class, g_driver_data->dev_num);
    }

    /* Remove device class */
    if (g_driver_data->sensor_class) {
        class_destroy(g_driver_data->sensor_class);
    }

    /* Remove character device */
    if (g_driver_data->sensor_cdev) {
        cdev_del(g_driver_data->sensor_cdev);
    }

    /* Release device number */
    unregister_chrdev_region(g_driver_data->dev_num, 1);

    /* Free map_buffer */
    if (g_driver_data && g_driver_data->map_buffer) {
        vfree(g_driver_data->map_buffer);
        g_driver_data->map_buffer = NULL;
        g_driver_data->map_buffer_size = 0;
    }

    /* Unmap shared memory */
    if (g_driver_data && g_driver_data->shared_mem_base) {
        iounmap(g_driver_data->shared_mem_base);
    }

    if (NULL != g_driver_data->data_record_proc_entry) {
        proc_remove(g_driver_data->data_record_proc_entry);
        g_driver_data->data_record_proc_entry = NULL;
    }
    /* Release driver private data */
    if (g_driver_data) {
        kfree(g_driver_data);
        g_driver_data = NULL;
    }

    INF("driver exited\n");
}

module_init(data_record_driver_init);
module_exit(data_record_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sensor Data Record Driver");
MODULE_DESCRIPTION("Sensor data transfer driver with mmap support");
