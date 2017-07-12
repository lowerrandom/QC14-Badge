/*
 * screen.c
 *
 *  Created on: Jun 12, 2017
 *      Author: George
 */
#include <string.h>

#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Swi.h>
#include "ExtFlash.h"

#include "tlc_driver.h"
#include "ui.h"
#include "screen.h"

extern uint8_t led_buf[11][7][3];

Task_Struct screen_anim_task;
char screen_anim_task_stack[512]; // 332

Clock_Handle screen_anim_clock_h;
Clock_Handle screen_blink_clock_h;

Semaphore_Handle anim_sem;

void screen_update_now() {
    Clock_stop(screen_anim_clock_h);
    Semaphore_post(anim_sem);
}

void screen_anim_tick(UArg a0) {
    Semaphore_post(anim_sem);
}

volatile uint8_t blink_status = 0;

void screen_blink_on() {
    blink_status = (ui_screen != UI_SCREEN_SLEEP);
    Clock_start(screen_blink_clock_h);
}

void screen_blink_off() {
    // Don't bother sending new `fun` data if blink_status is 0. Of course,
    // blink_status==0 implies the lights are lit, but since we invert
    // blink_status _after_ we set the bit in the `fun` buffer, actually
    // blink_status==0 means we're in the off part of a cycle.
    // also, lol, look a 3-deep stack of `blink_status` in this comment.
    if (!blink_status)
        led_blank_set(0);
    Clock_stop(screen_blink_clock_h);
}

void screen_blink_tick(UArg a0) {
    led_blank_set(blink_status);
    blink_status = !blink_status;
}

uint8_t rainbow_colors[6][3] = {{255,0,0}, {255,30,0}, {255,255,0}, {0,255,0}, {0,0,255}, {98,0,255}};
screen_frame_t power_bmp = {{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}}, {{255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}}, {{255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}}, {{255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}}, {{0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}}, {{0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}}}};
screen_frame_t tile_placeholder = {{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{0, 0, 0}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {0, 0, 0}}, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}};
screen_frame_t game_placeholder = {{{{0, 0, 0}, {0, 0, 0}, {11, 11, 11}, {255, 255, 255}, {11, 11, 11}, {0, 0, 0}, {0, 0, 0}}, {{0, 0, 0}, {0, 0, 0}, {105, 105, 105}, {64, 64, 64}, {104, 104, 104}, {0, 0, 0}, {0, 0, 0}}, {{0, 0, 0}, {1, 1, 1}, {170, 170, 170}, {0, 0, 0}, {174, 174, 174}, {1, 1, 1}, {0, 0, 0}}, {{0, 0, 0}, {54, 54, 54}, {35, 35, 35}, {0, 0, 0}, {36, 36, 36}, {53, 53, 53}, {0, 0, 0}}, {{0, 0, 0}, {208, 208, 208}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {208, 208, 208}, {0, 0, 0}}, {{22, 22, 22}, {81, 81, 81}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {81, 81, 81}, {21, 21, 21}}, {{142, 142, 142}, {5, 5, 5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {5, 5, 5}, {141, 141, 141}}}};

screen_frame_t master_anim_buf[] = {
// frame 0
{{{{0, 37, 44}, {0, 48, 46}, {0, 62, 46}, {0, 78, 42}, {3, 94, 29}, {18, 102, 16}, {46, 105, 9}}, {{2, 28, 41}, {0, 38, 44}, {0, 56, 46}, {0, 78, 41}, {8, 98, 23}, {45, 105, 10}, {87, 108, 5}}, {{6, 16, 36}, {3, 22, 39}, {0, 38, 45}, {0, 82, 39}, {45, 108, 10}, {127, 111, 2}, {168, 111, 1}}, {{17, 7, 30}, {17, 7, 30}, {15, 6, 31}, {55, 26, 13}, {215, 99, 1}, {240, 108, 0}, {245, 111, 0}}, {{39, 2, 23}, {50, 1, 21}, {86, 0, 16}, {178, 0, 7}, {238, 24, 1}, {245, 61, 1}, {248, 78, 0}}, {{64, 0, 18}, {86, 0, 16}, {122, 0, 12}, {170, 0, 7}, {212, 5, 3}, {231, 23, 2}, {235, 43, 1}}, {{86, 0, 16}, {108, 0, 13}, {137, 0, 10}, {170, 0, 7}, {201, 2, 4}, {221, 11, 2}, {231, 24, 2}}}, 0},
// frame 1
{{{{13, 11, 31}, {7, 15, 36}, {1, 28, 41}, {0, 41, 45}, {0, 61, 46}, {0, 85, 36}, {0, 85, 39}}, {{25, 5, 27}, {12, 10, 32}, {3, 22, 39}, {0, 44, 46}, {0, 70, 43}, {0, 86, 37}, {2, 91, 31}}, {{53, 3, 19}, {37, 4, 23}, {15, 10, 30}, {0, 42, 46}, {0, 85, 39}, {7, 98, 24}, {17, 102, 17}}, {{110, 2, 11}, {98, 2, 12}, {80, 1, 15}, {56, 26, 13}, {63, 114, 7}, {68, 108, 7}, {68, 107, 7}}, {{87, 0, 15}, {89, 0, 15}, {107, 0, 13}, {226, 10, 2}, {250, 97, 0}, {199, 113, 1}, {159, 110, 1}}, {{101, 0, 14}, {89, 0, 15}, {163, 1, 7}, {221, 10, 3}, {240, 54, 1}, {248, 105, 0}, {238, 111, 0}}, {{83, 0, 15}, {122, 0, 10}, {206, 2, 4}, {217, 10, 3}, {231, 28, 1}, {245, 83, 0}, {243, 107, 0}}}, 0},
// frame 2
{{{{102, 9, 10}, {29, 3, 26}, {15, 8, 30}, {5, 20, 37}, {39, 63, 13}, {65, 97, 7}, {1, 55, 40}}, {{125, 7, 8}, {87, 7, 12}, {22, 6, 28}, {8, 22, 32}, {46, 80, 11}, {3, 61, 33}, {1, 33, 42}}, {{86, 0, 15}, {113, 2, 10}, {71, 5, 15}, {22, 26, 22}, {15, 54, 22}, {17, 14, 27}, {50, 8, 17}}, {{69, 0, 18}, {63, 0, 19}, {87, 0, 15}, {73, 28, 10}, {12, 67, 22}, {6, 72, 27}, {1, 93, 33}}, {{64, 1, 18}, {113, 1, 11}, {184, 3, 5}, {189, 56, 2}, {134, 104, 2}, {36, 108, 11}, {19, 102, 16}}, {{182, 1, 6}, {212, 4, 4}, {212, 13, 3}, {142, 76, 3}, {243, 87, 0}, {161, 94, 2}, {57, 107, 8}}, {{201, 2, 4}, {221, 14, 2}, {193, 12, 3}, {121, 97, 3}, {221, 83, 1}, {248, 87, 0}, {180, 94, 1}}}, 0},
// frame 3
{{{{83, 0, 16}, {68, 0, 18}, {125, 4, 9}, {89, 6, 12}, {15, 7, 30}, {5, 18, 37}, {4, 21, 38}}, {{65, 0, 18}, {80, 0, 17}, {99, 1, 12}, {86, 6, 12}, {8, 12, 36}, {4, 22, 38}, {10, 40, 27}}, {{36, 4, 23}, {44, 2, 22}, {72, 0, 17}, {60, 4, 17}, {13, 26, 28}, {43, 72, 12}, {62, 86, 8}}, {{195, 1, 5}, {168, 3, 6}, {141, 4, 7}, {61, 26, 12}, {26, 54, 17}, {32, 67, 15}, {18, 81, 18}}, {{228, 13, 2}, {197, 16, 3}, {191, 60, 1}, {132, 102, 2}, {6, 71, 26}, {20, 19, 24}, {32, 7, 23}}, {{155, 24, 4}, {148, 78, 2}, {219, 94, 1}, {176, 95, 1}, {28, 107, 13}, {2, 95, 29}, {7, 52, 27}}, {{137, 95, 2}, {193, 97, 1}, {238, 86, 0}, {189, 94, 1}, {58, 107, 8}, {13, 99, 19}, {1, 95, 33}}}, 0},
// frame 4
{{{{148, 0, 8}, {52, 2, 20}, {68, 0, 18}, {89, 0, 15}, {121, 7, 9}, {40, 5, 22}, {24, 4, 27}}, {{204, 1, 5}, {127, 1, 10}, {60, 1, 19}, {91, 1, 14}, {95, 7, 11}, {26, 4, 26}, {11, 11, 33}}, {{219, 9, 3}, {217, 4, 3}, {122, 1, 10}, {89, 1, 14}, {35, 6, 23}, {9, 12, 34}, {6, 17, 36}}, {{182, 18, 3}, {182, 26, 3}, {201, 28, 2}, {71, 25, 11}, {22, 42, 20}, {20, 48, 20}, {20, 46, 20}}, {{153, 95, 2}, {184, 94, 1}, {219, 98, 0}, {27, 99, 14}, {3, 51, 33}, {6, 64, 27}, {33, 83, 13}}, {{219, 87, 1}, {245, 89, 0}, {153, 97, 2}, {16, 105, 17}, {3, 68, 31}, {11, 19, 29}, {0, 47, 46}}, {{233, 94, 0}, {238, 86, 0}, {80, 105, 6}, {17, 102, 17}, {1, 93, 34}, {23, 25, 21}, {30, 9, 23}}}, 0},
// frame 5
{{{{221, 12, 2}, {219, 12, 2}, {201, 1, 5}, {122, 0, 10}, {108, 0, 13}, {95, 0, 15}, {104, 1, 12}}, {{228, 25, 2}, {221, 13, 2}, {212, 5, 3}, {139, 0, 9}, {99, 0, 14}, {105, 1, 12}, {90, 2, 13}}, {{248, 81, 0}, {240, 50, 1}, {231, 17, 2}, {170, 0, 7}, {98, 0, 13}, {69, 3, 16}, {44, 2, 22}}, {{245, 110, 0}, {240, 110, 0}, {212, 97, 1}, {59, 23, 12}, {19, 6, 29}, {17, 7, 30}, {17, 7, 29}}, {{163, 111, 1}, {119, 110, 3}, {40, 107, 11}, {0, 59, 25}, {0, 35, 42}, {2, 24, 40}, {5, 18, 37}}, {{81, 105, 5}, {38, 99, 11}, {7, 102, 26}, {0, 37, 18}, {0, 43, 34}, {0, 44, 46}, {1, 33, 42}}, {{33, 95, 12}, {17, 101, 17}, {2, 97, 33}, {0, 41, 17}, {0, 35, 23}, {0, 56, 48}, {0, 45, 45}}}, 0},
// frame 6
{{{{219, 98, 0}, {204, 86, 1}, {166, 54, 2}, {226, 13, 2}, {204, 2, 4}, {168, 0, 7}, {87, 0, 15}}, {{206, 104, 1}, {212, 99, 1}, {182, 71, 1}, {215, 13, 2}, {186, 1, 6}, {86, 0, 15}, {70, 0, 17}}, {{199, 95, 1}, {215, 98, 1}, {221, 95, 0}, {215, 13, 2}, {122, 0, 11}, {76, 0, 17}, {89, 0, 15}}, {{54, 107, 8}, {61, 104, 7}, {61, 108, 7}, {64, 27, 12}, {73, 1, 16}, {94, 1, 13}, {110, 2, 11}}, {{12, 99, 20}, {4, 97, 28}, {1, 75, 36}, {15, 59, 22}, {16, 10, 29}, {35, 3, 24}, {46, 2, 21}}, {{1, 91, 35}, {3, 60, 32}, {2, 40, 36}, {40, 81, 12}, {9, 27, 30}, {11, 10, 33}, {24, 5, 27}}, {{2, 69, 33}, {10, 29, 28}, {1, 50, 40}, {65, 93, 7}, {12, 41, 25}, {6, 15, 37}, {10, 12, 33}}}, 0},
// frame 7
{{{{159, 105, 2}, {215, 105, 0}, {228, 108, 0}, {226, 72, 1}, {219, 17, 2}, {219, 12, 2}, {201, 2, 4}}, {{82, 108, 5}, {153, 107, 2}, {228, 110, 0}, {226, 67, 1}, {224, 13, 2}, {208, 3, 4}, {182, 1, 6}}, {{31, 104, 12}, {53, 107, 8}, {142, 113, 2}, {240, 59, 1}, {193, 3, 5}, {125, 0, 10}, {89, 1, 15}}, {{4, 94, 27}, {4, 94, 27}, {6, 99, 25}, {57, 27, 13}, {108, 0, 13}, {89, 0, 15}, {95, 0, 15}}, {{0, 86, 36}, {0, 78, 41}, {0, 59, 47}, {4, 17, 39}, {60, 2, 18}, {104, 1, 12}, {99, 0, 13}}, {{0, 72, 41}, {0, 59, 46}, {0, 42, 45}, {4, 20, 38}, {21, 6, 28}, {59, 2, 18}, {105, 4, 11}}, {{0, 59, 46}, {0, 48, 46}, {0, 36, 44}, {4, 20, 38}, {15, 9, 31}, {30, 3, 25}, {57, 2, 19}}}, 0},
// frame 8
{{{{4, 94, 28}, {15, 101, 18}, {60, 107, 7}, {180, 98, 1}, {195, 110, 1}, {228, 102, 0}, {215, 69, 1}}, {{1, 87, 36}, {3, 97, 28}, {37, 105, 11}, {168, 102, 1}, {210, 111, 0}, {204, 69, 1}, {178, 25, 3}}, {{5, 35, 33}, {0, 65, 39}, {5, 94, 26}, {139, 113, 2}, {219, 61, 1}, {206, 16, 3}, {224, 12, 2}}, {{1, 55, 38}, {3, 59, 33}, {2, 64, 34}, {52, 28, 14}, {165, 4, 6}, {184, 3, 5}, {206, 2, 4}}, {{23, 81, 16}, {16, 62, 21}, {8, 26, 31}, {50, 3, 19}, {71, 0, 17}, {58, 1, 19}, {64, 2, 17}}, {{6, 42, 31}, {4, 21, 37}, {9, 10, 34}, {72, 5, 15}, {98, 1, 13}, {65, 0, 19}, {63, 1, 18}}, {{4, 19, 38}, {7, 15, 36}, {17, 7, 30}, {75, 6, 15}, {117, 3, 10}, {73, 0, 17}, {63, 0, 19}}}, 0},
// frame 9
{{{{3, 40, 35}, {2, 57, 34}, {2, 95, 31}, {24, 102, 15}, {87, 108, 5}, {182, 105, 1}, {233, 110, 0}}, {{0, 50, 46}, {3, 38, 36}, {1, 82, 35}, {22, 104, 15}, {129, 107, 2}, {228, 111, 0}, {238, 110, 0}}, {{3, 60, 32}, {1, 56, 39}, {0, 65, 44}, {23, 108, 15}, {215, 111, 0}, {238, 93, 0}, {231, 70, 1}}, {{15, 55, 22}, {7, 44, 29}, {1, 33, 40}, {54, 28, 13}, {233, 23, 2}, {217, 20, 2}, {217, 14, 2}}, {{5, 18, 37}, {7, 13, 36}, {31, 6, 24}, {82, 1, 15}, {129, 1, 10}, {212, 4, 4}, {215, 10, 3}}, {{11, 11, 33}, {23, 5, 28}, {83, 5, 14}, {93, 1, 14}, {65, 1, 18}, {137, 0, 9}, {201, 1, 5}}, {{26, 4, 26}, {48, 4, 20}, {114, 5, 10}, {91, 0, 14}, {60, 1, 19}, {65, 1, 17}, {148, 0, 8}}}, 0},
}; // len: 10

inline void screen_put_buffer(screen_frame_t frame) {
    memcpy(led_buf, &frame, 7*7*3);
}

inline void screen_put_buffer_indirect(uint32_t frame_id) {
    memcpy(led_buf, &master_anim_buf[frame_id], 7*7*3);
}

inline void screen_put_buffer_from_flash(uint32_t frame_id) {
    // TODO: There's going to be a load from flash here, actually.
    memcpy(led_buf, &master_anim_buf[frame_id], 7*7*3);
}

void screen_anim_task_fn(UArg a0, UArg a1) {
    screen_anim_t load_anim = {
        0,
        10,
        100
    };
    uint16_t frame_index = 0;

    screen_anim_t *current_anim = &load_anim;

    while (1) {
        Semaphore_pend(anim_sem, BIOS_WAIT_FOREVER);
        switch(ui_screen) {
        case UI_SCREEN_GAME:
            // TODO: Do arms?
        case UI_SCREEN_GAME_SEL: // same, but blinking. and no arms.
            screen_put_buffer_indirect(current_anim->anim_start_frame
                                       + frame_index);
            frame_index += 1;
            if (frame_index == current_anim->anim_len) {
                // TODO: looping heeeeere.
                frame_index = 0;
            }
            Clock_setTimeout(screen_anim_clock_h,
                             current_anim->anim_frame_delay_ms * 100);
            Clock_start(screen_anim_clock_h);
            continue;

        case UI_SCREEN_TILE:
        case UI_SCREEN_TILE_SEL:
//            memcpy(led_buf, tile_placeholder, sizeof tile_placeholder);
            screen_put_buffer(tile_placeholder);
            break;
        case UI_SCREEN_SLEEP:
//            memcpy(led_buf, power_bmp, 147);
            screen_put_buffer(power_bmp);
            break;
        case UI_SCREEN_SLEEPING:
            memset(led_buf, 0, sizeof led_buf);
        }

        Clock_setTimeout(screen_anim_clock_h, 500000); // 5 seconds
        Clock_start(screen_anim_clock_h);
    }
}

void screen_init() {
    Semaphore_Params params;
    Semaphore_Params_init(&params);
    anim_sem = Semaphore_create(0, &params, NULL);

    Task_Params taskParams;
    Task_Params_init(&taskParams);
    taskParams.stack = screen_anim_task_stack;
    taskParams.stackSize = sizeof(screen_anim_task_stack);
    taskParams.priority = 1;
    Task_construct(&screen_anim_task, screen_anim_task_fn, &taskParams, NULL);

    Clock_Params clockParams;
    Clock_Params_init(&clockParams);
    clockParams.period = 0; // One-shot clock.
    clockParams.startFlag = TRUE;
    screen_anim_clock_h = Clock_create(screen_anim_tick, 100, &clockParams, NULL); // Wait 100 ticks (1ms) before firing for the first time.

    Clock_Params blink_clock_params;
    Clock_Params_init(&blink_clock_params);
    blink_clock_params.period = 50000; // 500 ms recurring
    blink_clock_params.startFlag = FALSE; // Don't auto-start (only when we blink-on)
    screen_blink_clock_h = Clock_create(screen_blink_tick, 0, &blink_clock_params, NULL);
}
