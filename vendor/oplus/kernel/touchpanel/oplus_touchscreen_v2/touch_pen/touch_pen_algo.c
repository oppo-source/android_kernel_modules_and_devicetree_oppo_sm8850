#include "touch_pen_core.h"

u8  press_smooth_time = 0;
u16  press_smooth_step = 0;
u8  press_smooth_direction = 0;
u16  press_smooth_last_val = 0;
u8  press_clear_flg = 0;

#define POINT_TYPE_STYLUS_HOVER      0x01
#define POINT_TYPE_STYLUS            0x03

int touch_pen_press_debounce(struct pen_info *info, u16 press_val, u16 last_press_val)
{
    u16 cur_press_val = press_val;
    static u8 press_filter_time = 0;

    PEN_DEBUG("start:press_val = %d,cur_press_val %d, point_type= %d status %d,"
        " last_press_val %d press_smooth_step %d press_clear_flg %d\n",
        press_val, cur_press_val, info->point_type, info->status,
        last_press_val, press_smooth_step, press_clear_flg);

    press_clear_flg = (press_val != 0 && last_press_val == 0) ? 0 : 1;
    if(info->status == 1) {
        if(press_val != 0 || last_press_val == 0) {
            press_filter_time = 0;
        } else {
            if ((press_filter_time < 3) && (info->point_type == POINT_TYPE_STYLUS) && (last_press_val < 200) &&
                (press_smooth_step < 40)) {
                cur_press_val = last_press_val;
                if((info->x > info->min_x) && (info->x < info->max_x) &&
                    (info->y > info->min_y) && (info->y < info->max_y)) {
                    press_filter_time++;
                } else {
                    press_filter_time += 2;
                }
            }else{
                press_filter_time = 0;
            }
        }
    } else {
        press_filter_time = 0;
    }

    PEN_DEBUG("end:press_val = %d,cur_press_val %d, point_type= %d status %d,"
        " last_press_val %d press_smooth_step %d press_clear_flg %d\n",
        press_val, cur_press_val, info->point_type, info->status,
        last_press_val, press_smooth_step, press_clear_flg);

    return cur_press_val;
}
EXPORT_SYMBOL(touch_pen_press_debounce);

void touch_pen_up_optimize(struct pen_info *info, u16 *cur_press_val, u16 *last_press_val)
{
    PEN_DEBUG("cur_press_val %d, last_press_val %d, point_type= %d press_clear_flg %d\n", *cur_press_val, *last_press_val,
        info->point_type, press_clear_flg);
    if (info->point_type == POINT_TYPE_STYLUS_HOVER && press_clear_flg == 1) {
        *cur_press_val = 0;
        *last_press_val = 0;
        press_smooth_last_val = 0;
    }
    press_clear_flg = 1;
    return;
}
EXPORT_SYMBOL(touch_pen_up_optimize);

void touch_pen_press_smooth_pre(u16 cur_press_val, u16 *last_press_val)
{
    PEN_DEBUG("start:cur_press_val = %d,last_press_val %d, press_smooth_time =%d val %d direction %d \n", cur_press_val, *last_press_val,
        press_smooth_time, press_smooth_step, press_smooth_direction);
    if (cur_press_val != 0 && *last_press_val != 0) {
        if (cur_press_val < *last_press_val) {
            press_smooth_direction = 0;
            press_smooth_step = (*last_press_val - cur_press_val) / 5;
        } else {
            press_smooth_direction = 1;
            press_smooth_step = (cur_press_val - *last_press_val) / 5;
        }
        if (press_smooth_step > PEN_SMOOTH_STEP_MAX) {
            press_smooth_step = PEN_SMOOTH_STEP_MAX;
        }
    } else {
        press_smooth_direction = 1;
        press_smooth_step = 0;
    }
    press_smooth_time = 0;

    *last_press_val = cur_press_val;
    PEN_DEBUG("end:cur_press_val = %d,last_press_val %d, press_smooth_time =%d val %d direction %d \n", cur_press_val, *last_press_val,
        press_smooth_time, press_smooth_step, press_smooth_direction);
}
EXPORT_SYMBOL(touch_pen_press_smooth_pre);

int touch_pen_press_smooth(u16 press_val)
{
    u32 smooth_step = 0;
    u16 cur_press_val = press_val;

    PEN_DEBUG("start:press_val = %d, press_smooth_time =%d val %d direction %d last_val %d\n", press_val,
        press_smooth_time, press_smooth_step, press_smooth_direction, press_smooth_last_val);

    if (press_val == 0) {
        press_smooth_last_val = 0;
        return 0;
    }
    if (press_smooth_time < PEN_SMOOTH_TIME_MAX) {
        smooth_step = (press_smooth_time > 0) ? press_smooth_step * press_smooth_time : 0;
        if(press_smooth_direction == 0) {
            if(cur_press_val > smooth_step) {
                cur_press_val -= smooth_step;
            }
            if ((press_smooth_last_val != 0) && (press_smooth_last_val < cur_press_val)) {
                cur_press_val = press_smooth_last_val;
            }
        } else {
            cur_press_val += smooth_step;
            cur_press_val = (cur_press_val > PEN_PRESS_MAX) ? PEN_PRESS_MAX : cur_press_val;
            if (press_smooth_last_val > cur_press_val) {
                cur_press_val = press_smooth_last_val;
            }
        }
        press_smooth_last_val = cur_press_val;
        press_smooth_time++;
    } else {
        cur_press_val = press_smooth_last_val;
    }

    PEN_DEBUG("end:cur_press_val = %d, press_smooth_time =%d val %d direction %d last_val %d\n", cur_press_val,
        press_smooth_time, press_smooth_step, press_smooth_direction, press_smooth_last_val);
    return cur_press_val;
}
EXPORT_SYMBOL(touch_pen_press_smooth);

/**
 * touch_pen_pressure_lift_detect - Detect pen pressure lift event
 * @state: Pointer to pen pressure state structure (must not be NULL)
 * @current_pressure: Current pressure value
 * @pen_diff: Current pen diff value from points->diff (e.g. from nvt_get_pen_points)
 * @pen_max_diff: Threshold from DTS; trigger only when pen_diff < pen_max_diff (if pen_max_diff > 0)
 *
 * This function modifies multiple members of the @state structure:
 * - cur_press: Updated with current_pressure
 * - max_press: May be updated when pen lift is detected
 * - is_pen_lift: May be updated based on pressure changes
 * - last_one_press: Updated with previous cur_press value
 * - last_two_press: Updated with previous last_one_press value
 *
 * IMPORTANT: This function does NOT provide any synchronization mechanism.
 * The caller MUST hold the appropriate mutex (e.g., pressure_mutex) before
 * calling this function to prevent race conditions when @state is accessed
 * from multiple execution contexts (e.g., interrupt handlers, work threads).
 *
 * Returns: true if pen lift is detected and all conditions are met, false otherwise
 */
bool touch_pen_pressure_lift_detect(struct pen_pressure_state *state, int current_pressure,
    int pen_diff, int pen_max_diff)
{
	int pressure_diff = 0;
	int threshold_press = 0;

	if (state == NULL) {
		PEN_DEBUG("%s: state is NULL\n", __func__);
		return false;
	}

	state->cur_press = current_pressure;
	PEN_DEBUG("%s: cur_press=%d, last_one_press=%d, last_two_press=%d, max_press=%d, is_pen_lift=%d\n",
		  __func__, state->cur_press, state->last_one_press,
		  state->last_two_press, state->max_press, state->is_pen_lift);

	if (state->last_one_press <= state->last_two_press &&
	    state->cur_press < state->last_one_press) {
		state->max_press = state->last_two_press;
		state->is_pen_lift = true;
		PEN_DEBUG("%s: pen lift detected, max_press=%d, is_pen_lift=true\n",
			  __func__, state->max_press);
	}

	if (state->last_one_press >= state->last_two_press &&
	    state->cur_press > state->last_one_press) {
		state->max_press = 0;
		state->is_pen_lift = false;
		PEN_DEBUG("%s: reset pen lift state, max_press=0, is_pen_lift=false\n", __func__);
	}

	pressure_diff = state->last_one_press - state->cur_press;
	threshold_press = state->max_press * state->pressure_ratio_threshold / 100;

	PEN_DEBUG("%s: check conditions - is_pen_lift=%d, cur_press=%d, threshold_press=%d, pressure_diff=%d, diff_threshold=%d, pen_diff=%d, pen_max_diff=%d\n",
		  __func__, state->is_pen_lift, state->cur_press, threshold_press,
		  pressure_diff, state->pressure_diff_threshold, pen_diff, pen_max_diff);

	if (state->is_pen_lift &&
	    (state->cur_press < threshold_press) &&
	    (pressure_diff > state->pressure_diff_threshold) &&
	    (pen_max_diff <= 0 || pen_diff < pen_max_diff)) {
		PEN_INFO("%s: all conditions met, need to disable 0g output\n", __func__);
		state->last_two_press = state->last_one_press;
		state->last_one_press = state->cur_press;
		return true;
	} else {
		if (!state->is_pen_lift) {
			PEN_DEBUG("%s: condition 1 failed: is_pen_lift=false\n", __func__);
		} else if (state->cur_press >= threshold_press) {
			PEN_DEBUG("%s: condition 2 failed: cur_press=%d >= threshold_press=%d\n",
				  __func__, state->cur_press, threshold_press);
		} else if (pressure_diff <= state->pressure_diff_threshold) {
			PEN_DEBUG("%s: condition 3 failed: pressure_diff=%d <= diff_threshold=%d\n",
				  __func__, pressure_diff, state->pressure_diff_threshold);
		} else if (pen_max_diff > 0 && pen_diff >= pen_max_diff) {
			PEN_DEBUG("%s: condition 4 failed: pen_diff=%d >= pen_max_diff=%d\n",
				  __func__, pen_diff, pen_max_diff);
		}
	}

	state->last_two_press = state->last_one_press;
	state->last_one_press = state->cur_press;
	PEN_DEBUG("%s: updated history - last_one_press=%d, last_two_press=%d\n",
		  __func__, state->last_one_press, state->last_two_press);

	return false;
}
EXPORT_SYMBOL(touch_pen_pressure_lift_detect);
