#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <linux/atomic.h>
#include <linux/overflow.h>
#include <linux/limits.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_MTK
#include "ged_kpi.h"
#endif

#include "gpu_ctrl.h"
#include "game_ctrl.h"
#include "frame_sync.h"

#define BQID_PKG_NAME_MAX_LEN 256
#define BQID_BUF_MAX_LEN 512

/* bqId requirement data structure */
struct bqid_requirement_data {
    char package_name[BQID_PKG_NAME_MAX_LEN];
    int need_bqid;  /* 0 = false, 1 = true */
    spinlock_t lock;
};

/* bqId statistics */
struct bqid_stats {
    unsigned long read_count;        /* bqid_requirement read count */
    unsigned long write_count;       /* bqid_requirement write count */
    unsigned long bqid_write_count;   /* bqid write count */
    unsigned long bqid_match_count;  /* bqid write with matching package name */
    unsigned long bqid_mismatch_count; /* bqid write with mismatched package name */
    unsigned long bqid_invalid_count;  /* bqid write with invalid format */
};

/* Global data for bqId nodes */
static struct bqid_requirement_data bqid_req_data;
static struct bqid_stats bqid_stats;
static DEFINE_SPINLOCK(bqid_stats_lock);

/* Procfs entries for bqId nodes */
static struct proc_dir_entry *bqid_req_proc = NULL;
static struct proc_dir_entry *bqid_proc = NULL;
static struct proc_dir_entry *bqid_stats_proc = NULL;

static struct gpu_ctrl_controller *gpu_ctrl = NULL;
atomic_t gpu_ctrl_per_frame_enable = ATOMIC_INIT(0);
static DEFINE_SPINLOCK(instance_lock);

/* Forward declarations for bqId procfs functions */
static int gpu_ctrl_bqid_proc_init(struct proc_dir_entry *gpu_ctrl_dir);
static void gpu_ctrl_bqid_proc_exit(struct proc_dir_entry *gpu_ctrl_dir);

struct gpu_ctrl_controller *get_instance(void) {
    struct gpu_ctrl_controller *ret = NULL;
    struct gpu_ctrl_controller *new_ctrl = NULL;

    ret = READ_ONCE(gpu_ctrl);
    if (likely(ret))
        return ret;

    spin_lock(&instance_lock);
    if (!gpu_ctrl) {
        spin_unlock(&instance_lock);
        new_ctrl = kzalloc(sizeof(struct gpu_ctrl_controller), GFP_KERNEL);
        if (!new_ctrl)
            return NULL;

        spin_lock_init(&new_ctrl->data_lock);
        new_ctrl->initialized = false;

        spin_lock(&instance_lock);
        if (!gpu_ctrl) {
            gpu_ctrl = new_ctrl;
            new_ctrl = NULL;
        }
    }
    ret = gpu_ctrl;
    spin_unlock(&instance_lock);

    if (new_ctrl)
        kfree(new_ctrl);

    return ret;
}

static int sync_ctrl_open(struct inode *inode, struct file *file) {
    struct gpu_ctrl_controller *gpu_ctrl = get_instance();

    if (!gpu_ctrl || !gpu_ctrl->initialized) {
        return -ENODEV;
    }

    return 0;
}

static int sync_ctrl_release(struct inode *inode, struct file *file) {
    return 0;
}

static void log_gpu_ctrl_data(const struct gpu_ctrl_data *data)
{
    pr_info("gameopt receive gpu ctrl data, bqId: %llu, targetFps: %d, targetFpsMargin: %d, earaFpsMargin: %d, cpuTime: %d, margin: %d, ctrlMode: %d\n",
        (unsigned long long)data->bqId, data->targetFps, data->targetFpsMargin,
        data->earaFpsMargin, data->cpuTime, data->margin, data->ctrlMode);
}

static int update_gpu_ctrl_data(struct gpu_ctrl_controller *gpu_ctrl,
                                 const struct gpu_ctrl_data *user_data,
                                 struct gpu_ctrl_data *logged_data)
{
    if (copy_from_user(logged_data, (void __user *)user_data, sizeof(*logged_data))) {
        return -EFAULT;
    }

    spin_lock(&gpu_ctrl->data_lock);
    u64 saved_bqId = gpu_ctrl->cur_data.bqId;
    gpu_ctrl->cur_data = *logged_data;
    if (saved_bqId != 0) {
        gpu_ctrl->cur_data.bqId = saved_bqId;
    }
    *logged_data = gpu_ctrl->cur_data;
    spin_unlock(&gpu_ctrl->data_lock);

    return 0;
}

static int handle_set_target_fps_margin(struct gpu_ctrl_controller *gpu_ctrl,
                                        unsigned long arg)
{
    struct gpu_ctrl_data data;
    int ret;

    atomic_set(&gpu_ctrl_per_frame_enable, 1);

    ret = update_gpu_ctrl_data(gpu_ctrl, (const struct gpu_ctrl_data *)arg, &data);
    if (ret) {
        return ret;
    }

    if (data.targetFps <= 0) {
        pr_err("gpu_ctrl, invalid targetFps: %d\n", data.targetFps);
        return -EINVAL;
    }
    if (data.bqId == 0) {
        pr_err("gpu_ctrl, invalid bqId: 0\n");
        return -EINVAL;
    }

    return 0;
}

static int handle_set_target_fps_api(struct gpu_ctrl_controller *gpu_ctrl,
                                     unsigned int cmd,
                                     unsigned long arg)
{
    struct gpu_ctrl_data data;
    int ret;

    if (cmd == GPU_CTRL_SET_TARGET_FPS_API_WITH_BQID) {
        atomic_set(&gpu_ctrl_per_frame_enable, 1);
    } else {
        atomic_set(&gpu_ctrl_per_frame_enable, 0);
    }

    ret = update_gpu_ctrl_data(gpu_ctrl, (const struct gpu_ctrl_data *)arg, &data);
    if (ret) {
        return ret;
    }

    if (data.targetFps <= 0) {
        pr_err("gpu_ctrl, invalid targetFps: %d\n", data.targetFps);
        return -EINVAL;
    }
    if (cmd == GPU_CTRL_SET_TARGET_FPS_API_WITH_BQID && data.bqId == 0) {
        pr_err("gpu_ctrl, invalid bqId for WITH_BQID mode: 0\n");
        return -EINVAL;
    }

    set_gpu_ctrl_with_instance(gpu_ctrl);

    return 0;
}

static int handle_disable_gpu_ctrl(struct gpu_ctrl_controller *gpu_ctrl,
                                    unsigned long arg)
{
    struct gpu_ctrl_data data;
    int ret;

    atomic_set(&gpu_ctrl_per_frame_enable, 0);

    ret = update_gpu_ctrl_data(gpu_ctrl, (const struct gpu_ctrl_data *)arg, &data);
    if (ret) {
        return ret;
    }

    log_gpu_ctrl_data(&data);
    return 0;
}

static int validate_ioctl_cmd(unsigned int cmd)
{
    if ((_IOC_TYPE(cmd) != GPU_IOC_MAGIC) || (_IOC_NR(cmd) >= GPU_CTRL_MAX_ID)) {
        return -EINVAL;
    }
    return 0;
}

static long dispatch_ioctl_cmd(struct gpu_ctrl_controller *gpu_ctrl,
                                unsigned int cmd,
                                unsigned long arg)
{
    switch (cmd) {
        case GPU_CTRL_SET_TARGET_FPS_MARGIN:
            return handle_set_target_fps_margin(gpu_ctrl, arg);
        case GPU_CTRL_SET_TARGET_FPS_API_WITHOUT_BQID:
        case GPU_CTRL_SET_TARGET_FPS_API_WITH_BQID:
            return handle_set_target_fps_api(gpu_ctrl, cmd, arg);
        case GPU_CTRL_DISABLE:
            return handle_disable_gpu_ctrl(gpu_ctrl, arg);
        default:
            return -ENOTTY;
    }
}

static long sync_ctrl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct gpu_ctrl_controller *gpu_ctrl;
    int ret;

    gpu_ctrl = get_instance();
    if (!gpu_ctrl || !gpu_ctrl->initialized) {
        return -ENODEV;
    }

    ret = validate_ioctl_cmd(cmd);
    if (ret) {
        return ret;
    }

    return dispatch_ioctl_cmd(gpu_ctrl, cmd, arg);
}

#if IS_ENABLED(CONFIG_COMPAT)
static long compat_sync_ctrl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return sync_ctrl_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif /* CONFIG_COMPAT */

static const struct proc_ops gpu_ctrl_proc_ops = {
    .proc_open = sync_ctrl_open,
    .proc_ioctl = sync_ctrl_ioctl,
    .proc_release = sync_ctrl_release,
#if IS_ENABLED(CONFIG_COMPAT)
    .proc_compat_ioctl = compat_sync_ctrl_ioctl,
#endif /* CONFIG_COMPAT */
};

int gpu_ctrl_init(void) {
    int ret;
    pr_info("gpu_ctrl, init\n");
    struct gpu_ctrl_controller *gpu_ctrl = get_instance();

    if (!gpu_ctrl) {
        pr_err("not found gpu_ctrl_controller\n");
        return -ENODEV;
    }

    if (unlikely(!game_opt_dir)) {
        pr_err("game_opt_dir is NULL\n");
        return -ENOTDIR;
    }

    gpu_ctrl->proc_dir = proc_mkdir("gpu_ctrl", game_opt_dir);
    if (!gpu_ctrl->proc_dir) {
        pr_err("gpu_ctrl dir init error, failed to create /proc/game_opt/gpu_ctrl\n");
        return -ENOMEM;
    }

    gpu_ctrl->proc_file = proc_create_data("ctrl_data", 0664, gpu_ctrl->proc_dir, &gpu_ctrl_proc_ops, NULL);
    if (!gpu_ctrl->proc_file) {
        pr_err("gpu_ctrl file init error, failed to create /proc/game_opt/gpu_ctrl/ctrl_data\n");
        remove_proc_entry("gpu_ctrl", game_opt_dir);
        gpu_ctrl->proc_dir = NULL;
        return -ENOMEM;
    }

    spin_lock(&gpu_ctrl->data_lock);
    gpu_ctrl->cur_data.bqId = 0;
    gpu_ctrl->cur_data.targetFps = -1;
    gpu_ctrl->cur_data.targetFpsMargin = 0;
    gpu_ctrl->cur_data.earaFpsMargin = 0;
    gpu_ctrl->cur_data.cpuTime = -1;
    gpu_ctrl->cur_data.margin = 0;
    gpu_ctrl->cur_data.ctrlMode = 0;
    gpu_ctrl->initialized = true;
    spin_unlock(&gpu_ctrl->data_lock);

    atomic_set(&gpu_ctrl_per_frame_enable, 0);

    ret = gpu_ctrl_bqid_proc_init(gpu_ctrl->proc_dir);
    if (ret) {
        pr_err("gpu_ctrl: failed to init bqId procfs nodes: %d\n", ret);
    }

    pr_info("gpu_ctrl, init success\n");

    return 0;
}

void gpu_ctrl_exit(void) {
    struct gpu_ctrl_controller *to_free = NULL;

    spin_lock(&instance_lock);

    if (!gpu_ctrl) {
        spin_unlock(&instance_lock);
        pr_info("gpu_ctrl, already exited\n");
        return;
    }

    spin_lock(&gpu_ctrl->data_lock);
    gpu_ctrl->initialized = false;
    spin_unlock(&gpu_ctrl->data_lock);

    if (gpu_ctrl->proc_dir) {
        gpu_ctrl_bqid_proc_exit(gpu_ctrl->proc_dir);
    }

    if (gpu_ctrl->proc_file) {
        remove_proc_entry("ctrl_data", gpu_ctrl->proc_dir);
        gpu_ctrl->proc_file = NULL;
    }
    if (gpu_ctrl->proc_dir) {
        remove_proc_entry("gpu_ctrl", game_opt_dir);
        gpu_ctrl->proc_dir = NULL;
    }

    to_free = gpu_ctrl;
    gpu_ctrl = NULL;
    atomic_set(&gpu_ctrl_per_frame_enable, 0);

    spin_unlock(&instance_lock);

    if (to_free) {
        kfree(to_free);
    }

    pr_info("gpu_ctrl, exit success\n");
}

static void set_gpu_ctrl_fps_margin(const struct gpu_ctrl_data *data, int target_fps_with_margin)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_MTK
    if (data->bqId != 0 && data->targetFps > 0 && data->cpuTime > 0) {
        ged_kpi_set_target_FPS_margin(
            data->bqId,
            target_fps_with_margin,
            data->targetFpsMargin,
            data->earaFpsMargin,
            data->cpuTime);
    }
    else {
        pr_err("gpu_ctrl, ged_kpi set ctrl data failed, bqId=%llu, targetFps=%d, cpuTime=%d\n",
               (unsigned long long)data->bqId, data->targetFps, data->cpuTime);
    }
#endif
}

static void set_gpu_ctrl_fps_api_without_bqid(const struct gpu_ctrl_data *data, int target_fps_with_margin)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_MTK
    ged_kpi_set_target_FPS_api(0, target_fps_with_margin, data->targetFpsMargin);
#endif
}

static void set_gpu_ctrl_fps_api_with_bqid(const struct gpu_ctrl_data *data, int target_fps_with_margin)
{
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_MTK
    if (data->bqId != 0) {
        ged_kpi_set_target_FPS_api(data->bqId, target_fps_with_margin, data->targetFpsMargin);
    } else {
        pr_err("gpu_ctrl, invalid bqId for mode FPS_API_WITH_BQID\n");
    }
#endif
}

void set_gpu_ctrl(void)
{
    struct gpu_ctrl_controller *gpu_ctrl = get_instance();
    set_gpu_ctrl_with_instance(gpu_ctrl);
}

void set_gpu_ctrl_with_instance(struct gpu_ctrl_controller *gpu_ctrl)
{
    if (!gpu_ctrl || !gpu_ctrl->initialized) {
        pr_err("gpu_ctrl, instance init error\n");
        return;
    }

    struct gpu_ctrl_data temp_data;
    spin_lock(&gpu_ctrl->data_lock);
    temp_data = gpu_ctrl->cur_data;
    spin_unlock(&gpu_ctrl->data_lock);

    int target_fps_with_margin;
    s64 target_sum = (s64)temp_data.targetFps + (s64)temp_data.margin;
    if (target_sum > INT_MAX || target_sum <= 0) {
        pr_err("gpu_ctrl, invalid target_fps_with_margin sum: %lld (targetFps=%d, margin=%d)\n",
               target_sum, temp_data.targetFps, temp_data.margin);
        return;
    }
    target_fps_with_margin = (int)target_sum;

    switch (temp_data.ctrlMode) {
        case GPU_CTRL_MODE_FPS_MARGIN:
            set_gpu_ctrl_fps_margin(&temp_data, target_fps_with_margin);
            break;
        case GPU_CTRL_MODE_FPS_API_WITHOUT_BQID:
            set_gpu_ctrl_fps_api_without_bqid(&temp_data, target_fps_with_margin);
            log_gpu_ctrl_data(&temp_data);
            break;
        case GPU_CTRL_MODE_FPS_API_WITH_BQID:
            set_gpu_ctrl_fps_api_with_bqid(&temp_data, target_fps_with_margin);
            break;
        default:
            pr_err("gpu_ctrl, invalid ctrlMode: %d\n", temp_data.ctrlMode);
    }
}

void gpu_ctrl_notify_frame_data(struct gameopt_frame_data *data, struct gameopt_frame_data *pre_data)
{
    if (!data || !pre_data) {
        pr_err("gpu_ctrl, ctrl data is null\n");
        return;
    }

    /* Skip first frame when pre_data timestamp is 0 */
    if (pre_data->timeStamp1 == 0) {
        pr_debug("gpu_ctrl, first frame, skip cpuTime calculation\n");
        return;
    }

    /* Current frame has invalid timestamp */
    if (data->timeStamp1 == 0) {
        pr_err("gpu_ctrl, current frame has invalid timestamp\n");
        return;
    }

    struct gpu_ctrl_controller *gpu_ctrl = get_instance();

    if (!gpu_ctrl || !gpu_ctrl->initialized) {
        pr_err("gpu_ctrl, instance init error\n");
        return;
    }

    u64 time_diff_u64;
    if (data->timeStamp1 < pre_data->timeStamp1) {
        pr_debug("gpu_ctrl, timestamp wraparound or reorder: cur=%llu, pre=%llu\n",
                 (unsigned long long)data->timeStamp1,
                 (unsigned long long)pre_data->timeStamp1);
        return;
    }

    time_diff_u64 = data->timeStamp1 - pre_data->timeStamp1;

    if (time_diff_u64 > INT_MAX) {
        pr_debug("gpu_ctrl, timestamp diff too large: %llu, clamping to %d\n",
                (unsigned long long)time_diff_u64, INT_MAX);
        time_diff_u64 = INT_MAX;
    }

    int clamped_time = (int)time_diff_u64;

    if (clamped_time <= 0) {
        pr_debug("gpu_ctrl, invalid timestamp diff: %d\n", clamped_time);
        return;
    }

    spin_lock(&gpu_ctrl->data_lock);
    gpu_ctrl->cur_data.cpuTime = clamped_time;
    spin_unlock(&gpu_ctrl->data_lock);

    set_gpu_ctrl_with_instance(gpu_ctrl);
}

/* Helper function: parse package name and value from buffer */
static int parse_package_value(const char *buf, size_t len,
                               char *pkg_name, size_t pkg_name_len,
                               int *value)
{
    char *colon_pos;
    char local_buf[BQID_BUF_MAX_LEN];
    int ret;

    if (len >= BQID_BUF_MAX_LEN) {
        pr_err("gpu_ctrl: buffer too large: %zu\n", len);
        return -EINVAL;
    }

    /* Copy to local buffer for parsing */
    memcpy(local_buf, buf, len);
    local_buf[len] = '\0';

    /* Find colon separator */
    colon_pos = strchr(local_buf, ':');
    if (!colon_pos) {
        pr_err("gpu_ctrl: invalid format, missing colon\n");
        return -EINVAL;
    }

    /* Extract package name */
    *colon_pos = '\0';
    if (strlen(local_buf) >= pkg_name_len) {
        pr_err("gpu_ctrl: package name too long\n");
        return -EINVAL;
    }
    strncpy(pkg_name, local_buf, pkg_name_len - 1);
    pkg_name[pkg_name_len - 1] = '\0';

    /* Extract value */
    ret = kstrtoint(colon_pos + 1, 10, value);
    if (ret) {
        pr_err("gpu_ctrl: invalid value format\n");
        return -EINVAL;
    }

    return 0;
}

/* Helper function: parse package name and bqId from buffer */
static int parse_package_bqid(const char *buf, size_t len,
                              char *pkg_name, size_t pkg_name_len,
                              u64 *bqid)
{
    char *colon_pos;
    char local_buf[BQID_BUF_MAX_LEN];
    int ret;

    if (len >= BQID_BUF_MAX_LEN) {
        pr_err("gpu_ctrl: buffer too large: %zu\n", len);
        return -EINVAL;
    }

    /* Copy to local buffer for parsing */
    memcpy(local_buf, buf, len);
    local_buf[len] = '\0';

    /* Find colon separator */
    colon_pos = strchr(local_buf, ':');
    if (!colon_pos) {
        pr_err("gpu_ctrl: invalid format, missing colon\n");
        return -EINVAL;
    }

    /* Extract package name */
    *colon_pos = '\0';
    if (strlen(local_buf) >= pkg_name_len) {
        pr_err("gpu_ctrl: package name too long\n");
        return -EINVAL;
    }
    strncpy(pkg_name, local_buf, pkg_name_len - 1);
    pkg_name[pkg_name_len - 1] = '\0';

    /* Extract bqId */
    ret = kstrtoull(colon_pos + 1, 10, bqid);
    if (ret) {
        pr_err("gpu_ctrl: invalid bqId format\n");
        return -EINVAL;
    }

    if (*bqid == 0) {
        pr_err("gpu_ctrl: bqId cannot be 0\n");
        return -EINVAL;
    }

    return 0;
}

/* bqid_requirement read handler */
static ssize_t bqid_requirement_read(struct file *file, char __user *buf,
                                     size_t count, loff_t *ppos)
{
    char page[BQID_BUF_MAX_LEN] = {0};
    int len;
    unsigned long flags;

    if (*ppos > 0) {
        return 0;
    }

    spin_lock_irqsave(&bqid_req_data.lock, flags);
    if (bqid_req_data.need_bqid == 1 && bqid_req_data.package_name[0] != '\0') {
        len = snprintf(page, sizeof(page), "%s:%d\n",
                       bqid_req_data.package_name, bqid_req_data.need_bqid);
    } else {
        len = snprintf(page, sizeof(page), ":%d\n", bqid_req_data.need_bqid);
    }
    spin_unlock_irqrestore(&bqid_req_data.lock, flags);

    if (len <= 0) {
        return -EIO;
    }

    if (len > count) {
        len = count;
    }

    if (copy_to_user(buf, page, len)) {
        return -EFAULT;
    }

    *ppos = len;

    spin_lock(&bqid_stats_lock);
    bqid_stats.read_count++;
    spin_unlock(&bqid_stats_lock);

    return len;
}

/* bqid_requirement write handler */
static ssize_t bqid_requirement_write(struct file *file, const char __user *buf,
                                       size_t count, loff_t *ppos)
{
    char local_buf[BQID_BUF_MAX_LEN] = {0};
    char pkg_name[BQID_PKG_NAME_MAX_LEN] = {0};
    int value;
    int ret;
    unsigned long flags;

    if (count == 0 || count >= BQID_BUF_MAX_LEN) {
        return -EINVAL;
    }

    if (copy_from_user(local_buf, buf, count)) {
        return -EFAULT;
    }

    /* Remove trailing newline if present */
    if (count > 0 && local_buf[count - 1] == '\n') {
        local_buf[count - 1] = '\0';
        count--;
    }

    /* Parse package name and value */
    ret = parse_package_value(local_buf, count, pkg_name, sizeof(pkg_name), &value);
    if (ret) {
        pr_err("gpu_ctrl: failed to parse bqid_requirement: %d\n", ret);
        return ret;
    }

    /* Validate value */
    if (value != 0 && value != 1) {
        pr_err("gpu_ctrl: invalid need_bqid value: %d (must be 0 or 1)\n", value);
        return -EINVAL;
    }

    /* Update bqid requirement data */
    spin_lock_irqsave(&bqid_req_data.lock, flags);
    strncpy(bqid_req_data.package_name, pkg_name, sizeof(bqid_req_data.package_name) - 1);
    bqid_req_data.package_name[sizeof(bqid_req_data.package_name) - 1] = '\0';
    bqid_req_data.need_bqid = value;
    spin_unlock_irqrestore(&bqid_req_data.lock, flags);

    /* Update statistics */
    spin_lock(&bqid_stats_lock);
    bqid_stats.write_count++;
    spin_unlock(&bqid_stats_lock);

    return count;
}

/* Helper: Update bqid statistics atomically */
static inline void update_bqid_stats(bool invalid, bool mismatch, bool success)
{
    spin_lock(&bqid_stats_lock);
    if (invalid)
        bqid_stats.bqid_invalid_count++;
    if (mismatch)
        bqid_stats.bqid_mismatch_count++;
    if (success) {
        bqid_stats.bqid_write_count++;
        bqid_stats.bqid_match_count++;
    }
    spin_unlock(&bqid_stats_lock);
}

/* Helper: Check if package name matches requirement */
static bool bqid_package_matches(const char *pkg_name, unsigned long *flags)
{
    bool match;

    spin_lock_irqsave(&bqid_req_data.lock, *flags);
    match = (bqid_req_data.need_bqid == 1 && strncmp(bqid_req_data.package_name, pkg_name, BQID_PKG_NAME_MAX_LEN) == 0);
    spin_unlock_irqrestore(&bqid_req_data.lock, *flags);

    return match;
}

/* Helper: Update bqId in gpu_ctrl data */
static bool bqid_update_controller(u64 bqid)
{
    struct gpu_ctrl_controller *gpu_ctrl = get_instance();

    if (!gpu_ctrl || !gpu_ctrl->initialized) {
        pr_err("gpu_ctrl: gpu_ctrl not initialized\n");
        return false;
    }

    spin_lock(&gpu_ctrl->data_lock);
    gpu_ctrl->cur_data.bqId = bqid;
    spin_unlock(&gpu_ctrl->data_lock);

    return true;
}

/* bqid write handler */
static ssize_t bqid_write(struct file *file, const char __user *buf,
                          size_t count, loff_t *ppos)
{
    char local_buf[BQID_BUF_MAX_LEN] = {0};
    char pkg_name[BQID_PKG_NAME_MAX_LEN] = {0};
    unsigned long flags;
    u64 bqid;
    int ret;

    /* Validate input size */
    if (count == 0 || count >= BQID_BUF_MAX_LEN)
        return -EINVAL;

    /* Copy from user space */
    if (copy_from_user(local_buf, buf, count))
        return -EFAULT;

    /* Remove trailing newline */
    if (count > 0 && local_buf[count - 1] == '\n') {
        local_buf[count - 1] = '\0';
        count--;
    }

    /* Parse input: "package_name:bqid" */
    ret = parse_package_bqid(local_buf, count, pkg_name, sizeof(pkg_name), &bqid);
    if (ret) {
        pr_err("gpu_ctrl: failed to parse bqid: %d\n", ret);
        update_bqid_stats(true, false, false);
        return ret;
    }

    /* Check if package name matches */
    if (!bqid_package_matches(pkg_name, &flags)) {
        pr_debug("gpu_ctrl: bqId rejected - package mismatch: '%s'\n", pkg_name);
        update_bqid_stats(false, true, false);
        return count;  /* Silent fail to avoid user space errors */
    }

    /* Update controller */
    if (!bqid_update_controller(bqid)) {
        update_bqid_stats(false, true, false);
        return count;  /* Silent fail */
    }

    /* Success */
    update_bqid_stats(false, false, true);
    pr_debug("gpu_ctrl: accepted bqId %llu for package '%s'\n",
             (unsigned long long)bqid, pkg_name);

    return count;
}

/* bqid_stats read handler */
static ssize_t bqid_stats_read(struct file *file, char __user *buf,
                                size_t count, loff_t *ppos)
{
    char page[512] = {0};
    int len;
    unsigned long flags;
    struct gpu_ctrl_controller *gpu_ctrl;
    u64 current_bqid = 0;

    if (*ppos > 0) {
        return 0;  /* EOF */
    }

    /* Get current bqId from gpu_ctrl_data */
    gpu_ctrl = get_instance();
    if (gpu_ctrl && gpu_ctrl->initialized) {
        spin_lock(&gpu_ctrl->data_lock);
        current_bqid = gpu_ctrl->cur_data.bqId;
        spin_unlock(&gpu_ctrl->data_lock);
    }

    spin_lock_irqsave(&bqid_stats_lock, flags);
    len = snprintf(page, sizeof(page),
                   "bqid_requirement_read: %lu\n"
                   "bqid_requirement_write: %lu\n"
                   "bqid_write: %lu\n"
                   "bqid_match: %lu\n"
                   "bqid_mismatch: %lu\n"
                   "bqid_invalid: %lu\n"
                   "current_package: %s\n"
                   "current_need_bqid: %d\n"
                   "current_bqid: %llu\n",
                   bqid_stats.read_count,
                   bqid_stats.write_count,
                   bqid_stats.bqid_write_count,
                   bqid_stats.bqid_match_count,
                   bqid_stats.bqid_mismatch_count,
                   bqid_stats.bqid_invalid_count,
                   bqid_req_data.package_name,
                   bqid_req_data.need_bqid,
                   (unsigned long long)current_bqid);
    spin_unlock_irqrestore(&bqid_stats_lock, flags);

    if (len <= 0) {
        return -EIO;
    }

    if (len > count) {
        len = count;
    }

    if (copy_to_user(buf, page, len)) {
        return -EFAULT;
    }

    *ppos = len;

    return len;
}

/* Procfs operations for bqId nodes */
static const struct proc_ops bqid_requirement_proc_ops = {
    .proc_read = bqid_requirement_read,
    .proc_write = bqid_requirement_write,
    .proc_lseek = default_llseek,
};

static const struct proc_ops bqid_proc_ops = {
    .proc_write = bqid_write,
    .proc_lseek = default_llseek,
};

static const struct proc_ops bqid_stats_proc_ops = {
    .proc_read = bqid_stats_read,
    .proc_lseek = default_llseek,
};

/* Initialize bqId procfs nodes */
static int gpu_ctrl_bqid_proc_init(struct proc_dir_entry *gpu_ctrl_dir)
{
    if (!gpu_ctrl_dir) {
        pr_err("gpu_ctrl: gpu_ctrl_dir is NULL\n");
        return -ENOTDIR;
    }

    /* Initialize bqid requirement data */
    spin_lock_init(&bqid_req_data.lock);
    memset(bqid_req_data.package_name, 0, sizeof(bqid_req_data.package_name));
    bqid_req_data.need_bqid = 0;

    /* Initialize statistics */
    memset(&bqid_stats, 0, sizeof(bqid_stats));

    /* Create bqid_requirement node (read/write) */
    bqid_req_proc = proc_create_data("bqid_requirement", 0664, gpu_ctrl_dir,
                                     &bqid_requirement_proc_ops, NULL);
    if (!bqid_req_proc) {
        pr_err("gpu_ctrl: failed to create bqid_requirement proc entry\n");
        return -ENOMEM;
    }

    /* Create bqid node (write only) */
    bqid_proc = proc_create_data("bqid", 0222, gpu_ctrl_dir,
                                  &bqid_proc_ops, NULL);
    if (!bqid_proc) {
        pr_err("gpu_ctrl: failed to create bqid proc entry\n");
        remove_proc_entry("bqid_requirement", gpu_ctrl_dir);
        bqid_req_proc = NULL;
        return -ENOMEM;
    }

    /* Create bqid_stats node (read only) */
    bqid_stats_proc = proc_create_data("bqid_stats", 0444, gpu_ctrl_dir,
                                       &bqid_stats_proc_ops, NULL);
    if (!bqid_stats_proc) {
        pr_err("gpu_ctrl: failed to create bqid_stats proc entry\n");
        remove_proc_entry("bqid", gpu_ctrl_dir);
        remove_proc_entry("bqid_requirement", gpu_ctrl_dir);
        bqid_proc = NULL;
        bqid_req_proc = NULL;
        return -ENOMEM;
    }

    pr_info("gpu_ctrl: bqId procfs nodes initialized successfully\n");
    pr_info("gpu_ctrl: /proc/game_opt/gpu_ctrl/bqid_requirement (rw)\n");
    pr_info("gpu_ctrl: /proc/game_opt/gpu_ctrl/bqid (w)\n");
    pr_info("gpu_ctrl: /proc/game_opt/gpu_ctrl/bqid_stats (r)\n");

    return 0;
}

/* Cleanup bqId procfs nodes */
static void gpu_ctrl_bqid_proc_exit(struct proc_dir_entry *gpu_ctrl_dir)
{
    if (!gpu_ctrl_dir) {
        return;
    }

    /* Remove procfs entries */
    if (bqid_stats_proc) {
        remove_proc_entry("bqid_stats", gpu_ctrl_dir);
        bqid_stats_proc = NULL;
    }

    if (bqid_proc) {
        remove_proc_entry("bqid", gpu_ctrl_dir);
        bqid_proc = NULL;
    }

    if (bqid_req_proc) {
        remove_proc_entry("bqid_requirement", gpu_ctrl_dir);
        bqid_req_proc = NULL;
    }

    pr_info("gpu_ctrl: bqId procfs nodes cleaned up\n");
}
