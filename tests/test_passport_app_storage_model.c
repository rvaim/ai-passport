#include "passport_app_storage_model.h"

#include <assert.h>
#include <stdio.h>

static void test_paths(void)
{
    assert(passport_app_storage_path_is_safe("state.json", false));
    assert(passport_app_storage_path_is_safe("profiles/user-1.json", false));
    assert(passport_app_storage_path_is_safe("a/b/c/value.bin", false));
    assert(passport_app_storage_path_is_safe("", true));

    assert(!passport_app_storage_path_is_safe("", false));
    assert(!passport_app_storage_path_is_safe(NULL, true));
    assert(!passport_app_storage_path_is_safe("/state.json", false));
    assert(!passport_app_storage_path_is_safe("../state.json", false));
    assert(!passport_app_storage_path_is_safe("a/./state.json", false));
    assert(!passport_app_storage_path_is_safe("a//state.json", false));
    assert(!passport_app_storage_path_is_safe(".system", false));
    assert(!passport_app_storage_path_is_safe("a/b/c/d/e", false));
    assert(!passport_app_storage_path_is_safe("state json", false));
}

static void test_quota(void)
{
    assert(passport_app_storage_allocated_bytes(0) == 0U);
    assert(passport_app_storage_allocated_bytes(1) == 4096U);
    assert(passport_app_storage_allocated_bytes(4096) == 4096U);
    assert(passport_app_storage_allocated_bytes(4097) == 8192U);

    assert(passport_app_storage_quota_allows(0, 0, 4096, 0, false));
    assert(passport_app_storage_quota_allows(65536, 4096, 4096, 16, true));
    assert(!passport_app_storage_quota_allows(65536, 0, 1, 16, false));
    assert(!passport_app_storage_quota_allows(0, 0, 4097, 0, false));
    assert(!passport_app_storage_quota_allows(0, 4096, 1, 0, true));
}

int main(void)
{
    test_paths();
    test_quota();
    puts("passport app storage model tests passed");
    return 0;
}
