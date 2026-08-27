#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_NAVIGATION_MAX_DEPTH 8

typedef struct {
    uint32_t route;
    int32_t state;
} passport_navigation_frame_t;

typedef struct {
    passport_navigation_frame_t frames[PASSPORT_NAVIGATION_MAX_DEPTH];
    uint8_t depth;
} passport_navigation_t;

void passport_navigation_reset(passport_navigation_t *navigation,
                               uint32_t root_route, int32_t state);
bool passport_navigation_push(passport_navigation_t *navigation,
                              uint32_t route, int32_t state);
bool passport_navigation_replace(passport_navigation_t *navigation,
                                 uint32_t route, int32_t state);
bool passport_navigation_pop(passport_navigation_t *navigation);
bool passport_navigation_set_state(passport_navigation_t *navigation, int32_t state);
const passport_navigation_frame_t *passport_navigation_current(
    const passport_navigation_t *navigation);
size_t passport_navigation_depth(const passport_navigation_t *navigation);
bool passport_navigation_can_pop(const passport_navigation_t *navigation);
