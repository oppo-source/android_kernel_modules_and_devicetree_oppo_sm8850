/** Copyright (C), 2019-2024, OPLUS Mobile Comm Corp., Ltd.
* Description: Hmbird camera freq boost
* Author: Gao ZhiFeng 80407967
* Create: 2025-11-17
* Notes: Hmbird camera freq boost
*/
#ifndef _HMBIRD_CAMERA_SCENE_H_
#define _HMBIRD_CAMERA_SCENE_H_

#define PIPELINE_STAGE_FINISH   	(0x7)
#define PIPELINE_STAGE_START		(0x0)
#define PIPELINE_STAGE_TRACE_END	(0x0)
#define MAX_PIPELINE_STAGES	    PIPELINE_STAGE_FINISH

typedef struct pipeline_status {
	unsigned long long start_time;
	unsigned long start_jiffies;
	unsigned long long stage;
	unsigned long delayed_tick_count;
	bool is_started;
} pipeline_status_t;

enum {
	PIPELINE_DELAYED = 0,
	PIPELINE_NDELAYED,
	PIPELINE_SLOWPATH,
};

extern raw_spinlock_t pipeline_lock;
extern pipeline_status_t g_pipeline[];
extern int boost_enable;

unsigned long check_pipeline_delayed_locked(void);
int hmbird_CameraScene_sysctl_init(void);
void hmbird_CameraScene_sysctl_deinit(void);

int hmbird_CameraScene_init(void);
void hmbird_CameraScene_exit(void);
#endif  /* _HMBIRD_CAMERA_SCENE_H_ */
