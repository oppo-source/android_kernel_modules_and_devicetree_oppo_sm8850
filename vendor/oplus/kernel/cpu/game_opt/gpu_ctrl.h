#ifndef __GPU_CTRL_H__
#define __GPU_CTRL_H__

#include "frame_sync.h"
#include <linux/atomic.h>

#define GPU_CTRL_MODE_DISABLE 0
#define GPU_CTRL_MODE_FPS_MARGIN 1
#define GPU_CTRL_MODE_FPS_API_WITHOUT_BQID 2
#define GPU_CTRL_MODE_FPS_API_WITH_BQID 3

struct gpu_ctrl_data {
    u64 bqId;
    int targetFps;
    int targetFpsMargin;
    int earaFpsMargin;
    int cpuTime;
    int margin;
    int ctrlMode;
};

struct gpu_ctrl_controller {
    struct gpu_ctrl_data cur_data;
    spinlock_t data_lock;
    struct proc_dir_entry *proc_dir;
    struct proc_dir_entry *proc_file;
    bool initialized;
};

enum gpu_ctrl_cmd_id {
    GPU_CTRL_DISABLE,
    GPU_CTRL_SET_TARGET_FPS_MARGIN,
    GPU_CTRL_SET_TARGET_FPS_API_WITHOUT_BQID,
    GPU_CTRL_SET_TARGET_FPS_API_WITH_BQID,
    GPU_CTRL_MAX_ID
};

#define GPU_IOC_MAGIC 0xDF
#define GPU_CTRL_DISABLE \
    _IOWR(GPU_IOC_MAGIC, GPU_CTRL_DISABLE, struct gpu_ctrl_data)
#define GPU_CTRL_SET_TARGET_FPS_MARGIN \
    _IOWR(GPU_IOC_MAGIC, GPU_CTRL_SET_TARGET_FPS_MARGIN, struct gpu_ctrl_data)
#define GPU_CTRL_SET_TARGET_FPS_API_WITHOUT_BQID \
    _IOWR(GPU_IOC_MAGIC, GPU_CTRL_SET_TARGET_FPS_API_WITHOUT_BQID, struct gpu_ctrl_data)
#define GPU_CTRL_SET_TARGET_FPS_API_WITH_BQID \
    _IOWR(GPU_IOC_MAGIC, GPU_CTRL_SET_TARGET_FPS_API_WITH_BQID, struct gpu_ctrl_data)


extern atomic_t gpu_ctrl_per_frame_enable;
struct gpu_ctrl_controller *get_instance(void);
int gpu_ctrl_init(void);
void gpu_ctrl_exit(void);
void set_gpu_ctrl(void);
void set_gpu_ctrl_with_instance(struct gpu_ctrl_controller *gpu_ctrl);
void gpu_ctrl_notify_frame_data(struct gameopt_frame_data *data, struct gameopt_frame_data *pre_data);

#endif  /*__GPU_CTRL_H__*/
