#include "passport_navigation.h"

#include <string.h>

void passport_navigation_reset(passport_navigation_t *navigation,
                               uint32_t root_route, int32_t state)
{
    if (!navigation) return;
    memset(navigation, 0, sizeof(*navigation));
    navigation->frames[0] = (passport_navigation_frame_t) {
        .route = root_route,
        .state = state,
    };
    navigation->depth = 1;
}

bool passport_navigation_push(passport_navigation_t *navigation,
                              uint32_t route, int32_t state)
{
    if (!navigation || navigation->depth == 0U ||
        navigation->depth >= PASSPORT_NAVIGATION_MAX_DEPTH) {
        return false;
    }
    navigation->frames[navigation->depth++] = (passport_navigation_frame_t) {
        .route = route,
        .state = state,
    };
    return true;
}

bool passport_navigation_replace(passport_navigation_t *navigation,
                                 uint32_t route, int32_t state)
{
    if (!navigation || navigation->depth == 0U) return false;
    navigation->frames[navigation->depth - 1U] = (passport_navigation_frame_t) {
        .route = route,
        .state = state,
    };
    return true;
}

bool passport_navigation_pop(passport_navigation_t *navigation)
{
    if (!passport_navigation_can_pop(navigation)) return false;
    --navigation->depth;
    memset(&navigation->frames[navigation->depth], 0,
           sizeof(navigation->frames[navigation->depth]));
    return true;
}

bool passport_navigation_set_state(passport_navigation_t *navigation, int32_t state)
{
    if (!navigation || navigation->depth == 0U) return false;
    navigation->frames[navigation->depth - 1U].state = state;
    return true;
}

const passport_navigation_frame_t *passport_navigation_current(
    const passport_navigation_t *navigation)
{
    if (!navigation || navigation->depth == 0U) return NULL;
    return &navigation->frames[navigation->depth - 1U];
}

size_t passport_navigation_depth(const passport_navigation_t *navigation)
{
    return navigation ? navigation->depth : 0U;
}

bool passport_navigation_can_pop(const passport_navigation_t *navigation)
{
    return navigation && navigation->depth > 1U;
}
