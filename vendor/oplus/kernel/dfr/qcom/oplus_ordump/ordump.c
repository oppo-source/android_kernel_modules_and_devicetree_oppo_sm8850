/*===========================================================================
* Fulldump Back 2.0
*
* Store Full Ramdump To Pre-allocated File ONLY FOR QCOM Platform
*
* This file contains logical writing lbaooo to smem.
*
*
* Date:2023-2-17
============================================================================*/
#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/soc/qcom/smem.h>
#ifdef OPLUS_FEATURE_FULLDUMP_BACK_SHRINK
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <drivers/android/debug_kinfo.h>
#endif

static int debug_magic = 0;
module_param(debug_magic, int, 0644);
MODULE_PARM_DESC(debug_magic, "debug magic number");

static unsigned long ordump_output_lbaooo = 0;
#ifdef OPLUS_FEATURE_FULLDUMP_BACK_SHRINK
static int shrinkdump_mode = 0;
#endif

#define SMEM_OPLUS_RDUMP    124
#define ORDUMP_EN_MAGIC     69
#define ENABLE              1
#define DISABLE             0

struct dump_lbaooo {
    unsigned long lbaooo;
    unsigned int enable;
    int debug_magic;
#ifdef OPLUS_FEATURE_FULLDUMP_BACK_SHRINK
    unsigned long page_array_phyaddr;
    int shrinkdump_enable;
    unsigned int page_struct_size;
#endif
};

#ifdef OPLUS_FEATURE_FULLDUMP_BACK_SHRINK
enum memory_states {
	MEMORY_ONLINE,
	MEMORY_OFFLINE,
	MAX_STATE,
};

struct segment_info {
	signed long start_addr;
	unsigned long seg_size;
	unsigned long num_kernel_blks;
	unsigned int bitmask_kernel_blk;
	enum memory_states state;
};

#define MAX_NUM_SEGMENTS 16
#define MAX_NUM_DDR_REGIONS 10

/*
 * start_addr_HIGH, start_addr_LOW,
 * length_HIGH, length_LOW,
 * segment_start_offset_HIGH, segment_start_offset_LOW,
 * segment_start_idx_HIGH, segment_start_idx_LOW,
 * granule_size_HIGH, granule_size_LOW
 */
#define DDR_REGIONS_NUM_CELLS       10

struct ddr_region {
	/* region physical address */
	unsigned long start_address;

	/* size of region in bytes */
	unsigned long length;

	/* size of segments in MB (1024 * 1024 bytes) */
	unsigned long granule_size;

	/* index of first full segment in a region */
	unsigned int segments_start_idx;

	/* offset in bytes to first full segment */
	unsigned long segments_start_offset;

};

struct page_info_struct {
	unsigned long start_address;
	unsigned long length;
	unsigned long *entries_ptr;
};

static struct page *page_start = (struct page*)VMEMMAP_START;
static struct page_info_struct *page_info_array = NULL;
static pgd_t *g_pgd = NULL;

static struct ddr_region *ddr_regions;
static int num_ddr_regions;

static unsigned int get_num_ddr_regions(struct device_node *node)
{
	int i, len;
	char str[20];

	for (i = 0; i < MAX_NUM_DDR_REGIONS; i++) {

		snprintf(str, sizeof(str), "region%d", i);
		if (!of_find_property(node, str, &len))
			break;
	}

	return i;
}

static int get_ddr_regions_info(void)
{
	struct device_node *node;
	struct property *prop;
	int len, num_cells;
	u64 val;
	int nr_address_cells;
	const __be32 *pos;
	char str[20];
	int i;

	node = of_find_node_by_name(of_root, "ddr-regions");
	if (!node) {
		pr_err("ordump: ddr-regions node not found in DT\n");
		return -EINVAL;
	}

	num_ddr_regions = get_num_ddr_regions(node);

	if (!num_ddr_regions) {
		pr_err("ordump: num_ddr_regions is %d\n", num_ddr_regions);
		return -EINVAL;
	}

	ddr_regions = kcalloc(num_ddr_regions, sizeof(*ddr_regions), GFP_KERNEL);
	if (!ddr_regions)
		return -ENOMEM;

	nr_address_cells = of_n_addr_cells(of_root);

	for (i = 0; i < num_ddr_regions; i++) {

		snprintf(str, sizeof(str), "region%d", i);
		prop = of_find_property(node, str, &len);

		if (!prop)
			return -EINVAL;

		num_cells = len / sizeof(__be32);
		if (num_cells != DDR_REGIONS_NUM_CELLS)
			return -EINVAL;

		pos = prop->value;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].start_address = val;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].length = val;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].segments_start_offset = val;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].segments_start_idx = val;

		val = of_read_number(pos, nr_address_cells);
		pos += nr_address_cells;
		ddr_regions[i].granule_size = val;
	}

	for (i = 0; i < num_ddr_regions; i++) {

		pr_info("region%d: seg_start 0x%lx len 0x%lx granule 0x%lx seg_start_offset 0x%lx seg_start_idx 0x%x\n",
				i, ddr_regions[i].start_address, ddr_regions[i].length,
				ddr_regions[i].granule_size,
				ddr_regions[i].segments_start_offset,
				ddr_regions[i].segments_start_idx);
	}

	return 0;
}


static pgd_t* get_pgd_vaddr(void)
{
	struct device_node *rmem_node;
	struct reserved_mem *rmem;
	struct kernel_all_info *kai;

	rmem_node = of_find_compatible_node(NULL, NULL, "google,debug-kinfo");
	if (!rmem_node) {
		pr_err("cannot get compatible node\n");
		goto out;
	}

	rmem_node = of_parse_phandle(rmem_node, "memory-region", 0);
	if (!rmem_node) {
		pr_err("cannot get memory region\n");
		goto out;
	}

	rmem = of_reserved_mem_lookup(rmem_node);
	if (!rmem) {
		pr_err("cannot get reserved memory\n");
		goto out;
	}

	kai = (struct kernel_all_info*)rmem->priv;
	if (!kai)
		goto out;

	return (pgd_t*)__phys_to_kimg(kai->info.swapper_pg_dir_pa);

out:
	return NULL;
}

static unsigned long virt_to_phys_vmemmap(unsigned long address, u64 *type)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;
	unsigned long paddr;
	struct mm_struct fake_init_mm;
	struct mm_struct *mm;

	fake_init_mm.pgd = g_pgd;

	mm = &fake_init_mm;

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		goto out;

	p4d = p4d_offset(pgd, address);
	if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d)))
		goto out;

	pud = pud_offset(p4d, address);
	if (pud_none(*pud) || unlikely(pud_bad(*pud)))
		goto out;

	pmd = pmd_offset(pud, address);
	pmdval_t pmdtype = pmd_val(*pmd) & PMD_TYPE_MASK;

	if (pmdtype == PMD_TYPE_SECT) {
		paddr = __pmd_to_phys(*pmd);
		*type = PMD_TYPE_SECT;
		return paddr;
	} else if (pmdtype == PMD_TYPE_TABLE) {
		ptep = pte_offset_kernel(pmd, address);
		if (!ptep)
			goto out;
		if (!pte_present(*ptep))
			goto out;

		paddr = __pte_to_phys(*ptep);
		*type = PMD_TYPE_TABLE;
		return paddr;
	}
out:
	*type = 0xff;
	return 0;
}

void record_page_addr_region(struct ddr_region *region, int region_no)
{
	//get ddr region size
	unsigned long num_pages = region->length / PAGE_SIZE;
	unsigned long page_entry_pt_num = PAGE_SIZE / sizeof(struct page);
	unsigned long page_entry_sect_num = PMD_SIZE / sizeof(struct page);
	unsigned long page_groups = num_pages / page_entry_pt_num;
	unsigned long memsz = sizeof(unsigned long) * page_groups;
	struct page *page_ptr = page_start + (region->start_address - PHYS_OFFSET) / PAGE_SIZE;

	int order = get_order(memsz);
	struct page *pg = alloc_pages(GFP_KERNEL, (unsigned int)order);
	if (!pg) {
		pr_err("alloc shrinkdump pages failed\n");
		return;
	}

	unsigned long *entry_start_ptr = (unsigned long*)page_to_virt(pg);
	page_info_array[region_no].entries_ptr = (unsigned long*)virt_to_phys(entry_start_ptr);
	page_info_array[region_no].start_address = region->start_address;
	page_info_array[region_no].length = region->length;

	for (unsigned long i = 0; i < page_groups;) {
		u64 pt_type;
		unsigned long paddr = virt_to_phys_vmemmap((unsigned long)page_ptr, &pt_type);
		if (pt_type == PMD_TYPE_SECT) {
			for (int j= 0; j < PTRS_PER_PTE; j++) {
				entry_start_ptr[i+j] = paddr;
				paddr += PAGE_SIZE;
			}
			page_ptr += page_entry_sect_num;
			i += PTRS_PER_PTE;
		} else if (pt_type == PMD_TYPE_TABLE) {
			entry_start_ptr[i] = paddr;
			page_ptr += page_entry_pt_num;
			i++;
		} else if (pt_type == 0xff) {
			paddr = 0; //no map
			for (int j = 0; j < PTRS_PER_PTE; j++) {
				entry_start_ptr[i+j] = paddr;
			}
			page_ptr += page_entry_sect_num;
			i += PTRS_PER_PTE;
		}
	}
}

static void dump_page_array_info(void)
{
	for (int i = 0; i < MAX_NUM_DDR_REGIONS; i++) {
		pr_info("[region%d]start:0x%lx len:0x%lx ptr:0x%lx\n", i, page_info_array[i].start_address,
							page_info_array[i].length,
							(unsigned long)page_info_array[i].entries_ptr);
	}
}

static void shrinkdump_cleanup(void)
{
	int i;
	int regions_to_clean = num_ddr_regions > 0 ? num_ddr_regions : 0;

	if (page_info_array) {
		for (i = 0; i < regions_to_clean; i++) {
			if (page_info_array[i].entries_ptr) {
				unsigned long virt_addr = (unsigned long)__va(page_info_array[i].entries_ptr);
				if (virt_addr) {
					unsigned long num_pages = page_info_array[i].length / PAGE_SIZE;
					unsigned long page_groups = num_pages / (PAGE_SIZE / sizeof(struct page));
					unsigned long memsz = sizeof(unsigned long) * page_groups;
					int order = get_order(memsz);
					free_pages(virt_addr, order);
					page_info_array[i].entries_ptr = NULL;
				}
			}
		}
		free_page((unsigned long)page_info_array);
		page_info_array = NULL;
	}

	if (ddr_regions) {
		kfree(ddr_regions);
		ddr_regions = NULL;
	}

	num_ddr_regions = 0;
	g_pgd = NULL;
	pr_info("shrinkdump cleanup done\n");
}

static int shrinkdump_init(struct dump_lbaooo *dl)
{
	int ret = 0;

    if (shrinkdump_mode == 0) {
        pr_info("shrinkdump mode is disabled, skip initialization\n");
        dl->page_array_phyaddr = 0;
        return 0;
    }

	g_pgd = get_pgd_vaddr();
	if (!g_pgd) {
		pr_err("g_pgd is null\n");
		return -EINVAL;
	}

	ret = get_ddr_regions_info();
	if (ret < 0) {
		pr_err("get ddr region info failed, shrinkdump not support\n");
		goto cleanup;
	}

	page_info_array = (struct page_info_struct*)get_zeroed_page(GFP_KERNEL);
	if (!page_info_array) {
		pr_err("get page_info_array page failed, shrinkdump not support\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	for (int i = 0; i < num_ddr_regions; i++) {
		record_page_addr_region(ddr_regions + i, i);
	}

	dl->page_array_phyaddr = (unsigned long)virt_to_phys(page_info_array);
	dl->page_struct_size = sizeof(struct page);
	pr_info("shrinkdump enabled, page_array_phyaddr=0x%lx, page_struct_size=%u\n",
	        dl->page_array_phyaddr, dl->page_struct_size);

	dump_page_array_info();

	return 0;

cleanup:
	if (ddr_regions) {
		kfree(ddr_regions);
		ddr_regions = NULL;
	}
	if (page_info_array) {
		free_page((unsigned long)page_info_array);
		page_info_array = NULL;
	}
	num_ddr_regions = 0;
	g_pgd = NULL;

	return ret;
}
#endif

static int __init ordump_sysfs_init(void)
{
    int ret = 0;
    struct dump_lbaooo *dump_lbaooo;
    size_t smem_size;
    unsigned int len = sizeof(struct dump_lbaooo);

    ret = qcom_smem_alloc(0, SMEM_OPLUS_RDUMP, len);
    if (ret < 0 && ret != -EEXIST) {
        pr_err("%s smem_alloc fail\n", __func__);
        return -EFAULT;
    }

    dump_lbaooo = (struct dump_lbaooo*)qcom_smem_get(0, SMEM_OPLUS_RDUMP, &smem_size);
    if (!IS_ERR(dump_lbaooo) && (dump_lbaooo->debug_magic == ORDUMP_EN_MAGIC)) {
        debug_magic = ENABLE;
        pr_info("%s: enable debug_magic .\n", __func__);
    }

#ifdef OPLUS_FEATURE_FULLDUMP_BACK_SHRINK
    if (!IS_ERR(dump_lbaooo) && smem_size >= sizeof(struct dump_lbaooo)) {
        shrinkdump_mode = dump_lbaooo->shrinkdump_enable;
        pr_info("%s: read shrinkdump mode from SMEM: %d\n", __func__, shrinkdump_mode);
    } else {
        pr_info("%s: use default shrinkdump mode: %d\n", __func__, shrinkdump_mode);
    }

    ret = shrinkdump_init(dump_lbaooo);
    if (ret < 0) {
        pr_err("%s: shrinkdump init failed, ret=%d\n", __func__, ret);
    }
#endif

    pr_info("%s: done.\n", __func__);
    return 0;
}

#ifdef OPLUS_FEATURE_FULLDUMP_BACK_SHRINK
static int param_set_shrinkdump_mode(const char *val, const struct kernel_param *kp)
{
    int retval, old_mode;
    struct dump_lbaooo *dump_lbaooo;
    size_t smem_size;

    old_mode = shrinkdump_mode;
    retval = param_set_int(val, kp);
    if (retval < 0) {
        return retval;
    }

    dump_lbaooo = (struct dump_lbaooo*)qcom_smem_get(0, SMEM_OPLUS_RDUMP, &smem_size);
    if (IS_ERR(dump_lbaooo)) {
        pr_err("%s smem_get fail\n", __func__);
        return -EFAULT;
    }

    if (!IS_ERR(dump_lbaooo) && smem_size >= sizeof(struct dump_lbaooo)) {
        dump_lbaooo->shrinkdump_enable = shrinkdump_mode;
        pr_info("%s: saved shrinkdump mode to SMEM: %d\n", __func__, shrinkdump_mode);
    } else if (!IS_ERR(dump_lbaooo)) {
        pr_warn("%s: SMEM too small (%zu < %zu), cannot save shrinkdump mode\n",
                __func__, smem_size, sizeof(struct dump_lbaooo));
    }

    if (old_mode == 1 && shrinkdump_mode == 0) {
        pr_info("%s: shrinkdump mode changed from enable to disable, cleanup memory\n", __func__);
        shrinkdump_cleanup();
        dump_lbaooo->page_array_phyaddr = 0;
    } else if (old_mode == 0 && shrinkdump_mode == 1) {
        pr_info("%s: shrinkdump mode changed from disable to enable, re-initialize\n", __func__);
        retval = shrinkdump_init(dump_lbaooo);
        if (retval < 0) {
            pr_err("%s: shrinkdump re-init failed, ret=%d\n", __func__, retval);
            shrinkdump_mode = old_mode;
            return retval;
        }
    }

    pr_info("%s: shrinkdump mode set to %d\n", __func__, shrinkdump_mode);
    return 0;
}

static int param_get_shrinkdump_mode(char *buffer, const struct kernel_param *kp)
{
    return param_get_int(buffer, kp);
}

struct kernel_param_ops param_ops_shrinkdump_mode = {
    .set = param_set_shrinkdump_mode,
    .get = param_get_shrinkdump_mode,
};
#endif

static int param_set_ordump_lbaooo(const char *val,
             const struct kernel_param *kp)
{
    int retval = 0;
    struct dump_lbaooo *dump_lbaooo;
    size_t smem_size;

    retval = param_set_ulong(val, kp);
    dump_lbaooo = (struct dump_lbaooo*)qcom_smem_get(0, SMEM_OPLUS_RDUMP, &smem_size);
    if (IS_ERR(dump_lbaooo)) {
        pr_err("%s smem_get fail\n", __func__);
        return -EFAULT;
    }

    if (ordump_output_lbaooo != 0) {
        pr_info("%s: OPLUS-RAMDUMP enabled,lbaooo = 0x%lx\n", __func__, ordump_output_lbaooo);
        dump_lbaooo->lbaooo = ordump_output_lbaooo;
        dump_lbaooo->enable = 1;
        dump_lbaooo->debug_magic = debug_magic;
    } else {
        pr_info("%s: OPLUS-RAMDUMP disabled!\n", __func__);
        dump_lbaooo->lbaooo = 0;
        dump_lbaooo->enable = 0;
        dump_lbaooo->debug_magic = 0;
    }
    return retval;
}

/**
 *        0644 : S_IRUGO | S_IWUSR
 *     IN TREE : /sys/module/ordump/parameters/lbaooo
 * OUT OF TREE : /sys/module/oplus_bsp_dfr_ordump/parameters/lbaooo
 */
struct kernel_param_ops param_ops_ordump_lbaooo = {
    .set = param_set_ordump_lbaooo,
    .get = param_get_ulong,
};

late_initcall(ordump_sysfs_init);

param_check_ulong(lbaooo, &ordump_output_lbaooo);
module_param_cb(lbaooo, &param_ops_ordump_lbaooo, &ordump_output_lbaooo, 0644);
__MODULE_PARM_TYPE(lbaooo, "unsigned long");

#ifdef OPLUS_FEATURE_FULLDUMP_BACK_SHRINK
param_check_int(shrinkdump_mode, &shrinkdump_mode);
module_param_cb(shrinkdump_mode, &param_ops_shrinkdump_mode, &shrinkdump_mode, 0644);
__MODULE_PARM_TYPE(shrinkdump_mode, "int");
#endif

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OPLUS RDUMP module");
MODULE_AUTHOR("Nick.Chen@BSP.Kernel.Stability");
