#include "passport_navigation.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    passport_navigation_t navigation = {0};
    assert(passport_navigation_depth(&navigation) == 0U);
    assert(!passport_navigation_can_pop(&navigation));
    assert(!passport_navigation_pop(&navigation));
    assert(!passport_navigation_push(&navigation, 1, 0));

    passport_navigation_reset(&navigation, 10, 3);
    assert(passport_navigation_depth(&navigation) == 1U);
    assert(passport_navigation_current(&navigation)->route == 10U);
    assert(passport_navigation_current(&navigation)->state == 3);
    assert(!passport_navigation_can_pop(&navigation));

    assert(passport_navigation_push(&navigation, 20, 7));
    assert(passport_navigation_can_pop(&navigation));
    assert(passport_navigation_current(&navigation)->route == 20U);
    assert(passport_navigation_set_state(&navigation, 9));
    assert(passport_navigation_current(&navigation)->state == 9);
    assert(passport_navigation_replace(&navigation, 21, 11));
    assert(passport_navigation_current(&navigation)->route == 21U);
    assert(passport_navigation_current(&navigation)->state == 11);
    assert(passport_navigation_pop(&navigation));
    assert(passport_navigation_current(&navigation)->route == 10U);

    for (uint32_t route = 1; route < PASSPORT_NAVIGATION_MAX_DEPTH; ++route) {
        assert(passport_navigation_push(&navigation, route, 0));
    }
    assert(passport_navigation_depth(&navigation) == PASSPORT_NAVIGATION_MAX_DEPTH);
    assert(!passport_navigation_push(&navigation, 99, 0));

    puts("Passport navigation host tests: PASS");
    return 0;
}
