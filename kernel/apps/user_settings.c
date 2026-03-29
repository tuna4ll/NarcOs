#include <stdint.h>
#include "usermode.h"
#include "user_abi.h"

#define USER_CODE __attribute__((section(".user_code")))

static USER_CODE int settings_dequeue_event(user_settings_state_t* state, int* out_type, int* out_value) {
    int head;

    if (!state || !out_type || !out_value) return 0;
    if (state->event_head == state->event_tail) return 0;
    head = state->event_head;
    *out_type = state->event_type[head];
    *out_value = state->event_arg[head];
    state->event_head = (head + 1) % USER_GUI_EVENT_QUEUE_CAP;
    return 1;
}

void USER_CODE user_settings_entry_c(user_settings_state_t* state) {
    if (!state) return;

    for (;;) {
        int event_type;
        int event_value;

        while (settings_dequeue_event(state, &event_type, &event_value)) {
            switch (event_type) {
                case USER_SETTINGS_EVT_ADJUST_OFFSET:
                    (void)user_set_timezone_offset_minutes(user_get_timezone_offset_minutes() + event_value);
                    (void)user_save_timezone_setting();
                    state->dirty = 1;
                    break;
                case USER_SETTINGS_EVT_SET_OFFSET:
                    (void)user_set_timezone_offset_minutes(event_value);
                    (void)user_save_timezone_setting();
                    state->dirty = 1;
                    break;
                case USER_SETTINGS_EVT_OPEN_CONFIG:
                    if (user_save_timezone_setting() == 0) {
                        (void)user_gui_open_narcpad_file("/system/timezone.cfg");
                    }
                    state->dirty = 1;
                    break;
                default:
                    break;
            }
        }
        user_yield();
    }
}
