/** Copyright (C), 2019-2024, OPLUS Mobile Comm Corp., Ltd.
* Description: Hmbird camera freq boost
* Author: Gao ZhiFeng 80407967
* Create: 2025-11-17
* Notes: Hmbird camera freq boost
*/
#ifndef __HMBIRD_CAMERA_BOOST_H__
#define __HMBIRD_CAMERA_BOOST_H__

#define MAX_FREQ_NODE 50
#define MAX_CLUSTER_NUM 5

struct cached_freq_idx {
	int idx_min;
	int idx_max;
	bool idx_cached;
};

void boost_soft_min_freq(int order);

#endif /* __HMBIRD_CAMERA_BOOST_H__ */
