#include <stdint.h>

#include "usermode.h"
#include "user_abi.h"

#define USER_CODE __attribute__((section(".user_code")))

static USER_CODE void snake_spawn_apple(user_snake_state_t* state) {
    if (!state) return;

    state->apple_x = (int)(user_random_u32() % 37U) + 1;
    state->apple_y = (int)(user_random_u32() % 27U) + 1;
}

static USER_CODE void snake_reset(user_snake_state_t* state) {
    if (!state) return;

    state->len = 5;
    state->dead = 0;
    state->score = 0;
    state->dir = 3;
    state->last_tick = 0;

    for (int i = 0; i < 5; i++) {
        state->px[i] = 10 - i;
        state->py[i] = 10;
    }

    snake_spawn_apple(state);
}

static USER_CODE int snake_direction_is_reverse(int current_dir, int next_dir) {
    return (current_dir == 0 && next_dir == 1) ||
           (current_dir == 1 && next_dir == 0) ||
           (current_dir == 2 && next_dir == 3) ||
           (current_dir == 3 && next_dir == 2);
}

static USER_CODE void snake_step(user_snake_state_t* state) {
    if (!state) return;

    for (int i = state->len - 1; i > 0; i--) {
        state->px[i] = state->px[i - 1];
        state->py[i] = state->py[i - 1];
    }

    if (state->dir == 0) state->py[0]--;
    else if (state->dir == 1) state->py[0]++;
    else if (state->dir == 2) state->px[0]--;
    else state->px[0]++;

    if (state->px[0] < 0 || state->px[0] >= 39 || state->py[0] < 0 || state->py[0] >= 29) {
        state->dead = 1;
        return;
    }

    for (int i = 1; i < state->len; i++) {
        if (state->px[0] == state->px[i] && state->py[0] == state->py[i]) {
            state->dead = 1;
            return;
        }
    }

    if (state->px[0] != state->apple_x || state->py[0] != state->apple_y) return;

    if (state->len < 100) state->len++;
    state->score += 10;
    if (state->score > state->best) state->best = state->score;
    snake_spawn_apple(state);
}

void USER_CODE user_snake_entry_c(user_snake_state_t* state) {
    if (!state) return;

    snake_reset(state);

    for (;;) {
        int input = user_snake_get_input();

        if (input == 6) {
            (void)user_snake_close();
            return;
        }
        if (input == 5) {
            snake_reset(state);
        } else if (input >= 0 && input <= 3 && !snake_direction_is_reverse(state->dir, input)) {
            state->dir = input;
        }

        if (!state->dead) {
            uint32_t now = user_uptime_ticks();

            if ((uint32_t)(now - (uint32_t)state->last_tick) > 10U) {
                state->last_tick = (int)now;
                snake_step(state);
            }
        }

        user_yield();
    }
}
