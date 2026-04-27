#ifndef __TOUCH_PEN_H
#define __TOUCH_PEN_H

#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/skmsg.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "../touchpanel_common.h"
#include "../touch_comon_api/touch_comon_api.h"

extern unsigned int tp_debug;

#define TOUCH_PEN_NAME "oplus_pen"

#define PEN_DEBUG(fmt, args...)\
    do{\
    if (LEVEL_DEBUG == tp_debug)\
        pr_info("[%s][%s][%d]:" fmt, TOUCH_PEN_NAME, __func__, __LINE__, ##args);\
    }while(0)

#define PEN_INFO(fmt, args...) \
        pr_info("[%s][%s][%d]:" fmt, \
            TOUCH_PEN_NAME, __func__, __LINE__, ##args)

#define PEN_WARN(fmt, args...)\
        pr_warn("[%s][%s][%d]:" fmt, \
            TOUCH_PEN_NAME, __func__, __LINE__, ##args)

#define PEN_ERR(fmt, args...) \
        pr_err("[%s][%s][%d]:" fmt, \
            TOUCH_PEN_NAME, __func__, __LINE__, ##args)

#define PEN_BUF_SIZE 64
#define PEN_MSG_NUM 32
#define PEN_MSG_BLOCK_BIT 6 // 64 bytes

#define PEN_PRESS_MAX 4095
#define PEN_SMOOTH_TIME_MAX 10
#define PEN_SMOOTH_STEP_MAX 40

enum touch_pen_uplk_cmd {
    PEN_UP_CMD_TIMING = 0,
    PEN_UP_CMD_HOP_FRQ,
    PEN_UP_CMD_GET_SIZE,
    PEN_UP_CMD_SPEED,
    PEN_UP_CMD_MAX,
};

enum touch_pen_downlk_cmd {
    PEN_DOWN_CMD_CFG_ID = 0,
    PEN_DOWN_CMD_CON_STA,
    PEN_DOWN_CMD_SIZE_INFO,
    PEN_DOWN_CMD_REQ_CFG,
    PEN_DOWN_CMD_TIMING_CFG_ACK,
    PEN_DOWN_CMD_HOPFRQ_CFG_ACK,
    PEN_DOWN_CMD_PRESS,
    PEN_DOWN_CMD_SPEED_SWITCH,
    PEN_DOWN_CMD_PEN_INFO,
    PEN_DOWN_CMD_MAX,
};

enum pen_buf_type {
    PEN_TYPE_NONE,
    PEN_TYPE_GET_CMD,
    PEN_TYPE_SET_CMD,
};

enum penctl_flag {
    PEN_CTL_FLAG_NONE = 0,
    PEN_CTL_FLAG_TIMEOUT = 0x1,
};

int touch_pen_init(struct touchpanel_data *ts);
void touch_pen_uninit(struct touchpanel_data *ts);
long touch_pen_uplink_msg_ioctl(struct touchpanel_data *ts, unsigned long arg);
long touch_pen_downlink_msg_ioctl(struct touchpanel_data *ts, unsigned long arg);
void touch_pen_uplink_msg_handle(struct touchpanel_data *ts);
void touch_pen_speed_handle(struct touchpanel_data *ts, u16 speed);

/* algo interface */
int touch_pen_press_debounce(struct pen_info *info, u16 press_val, u16 last_press_val);
void touch_pen_up_optimize(struct pen_info *info, u16 *cur_press_val, u16 *last_press_val);
void touch_pen_press_smooth_pre(u16 cur_press_val, u16 *last_press_val);
int touch_pen_press_smooth(u16 press_val);

/* pen pressure lift detection */
struct pen_pressure_state {
	int cur_press;              /* Current frame pressure value */
	int last_one_press;         /* Previous frame pressure value */
	int last_two_press;         /* Pressure value two frames ago */
	int max_press;              /* Maximum pressure value when pen starts to lift */
	bool is_pen_lift;           /* Flag to indicate if pen is starting to lift */
	int pressure_ratio_threshold; /* Pressure ratio threshold (stored as integer, e.g., 30 for 0.3) */
	int pressure_diff_threshold;  /* Pressure difference threshold (default: 300) */
};

bool touch_pen_pressure_lift_detect(struct pen_pressure_state *state, int current_pressure,
    int pen_diff, int pen_max_diff);

#endif
