#include "passport_runtime_storage.h"

#include "lauxlib.h"
#include "lualib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOCK_JOB_MAX 12U

typedef struct {
    passport_app_storage_operation_t operation;
    uint32_t request_id;
    char app_id[PASSPORT_MANIFEST_ID_MAX];
    char path[PASSPORT_APP_STORAGE_PATH_MAX];
    uint8_t data[PASSPORT_APP_STORAGE_FILE_MAX];
    size_t data_size;
    passport_app_storage_completion_cb_t callback;
    void *user;
} mock_job_t;

static mock_job_t s_jobs[MOCK_JOB_MAX];
static size_t s_job_count;
static passport_app_storage_completion_t *s_dispatched;
static size_t s_completion_free_count;

static passport_app_storage_error_t submit_mock(
    passport_app_storage_operation_t operation, const char *app_id,
    const char *path, const void *data, size_t data_size, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    if (path && strcmp(path, "../bad") == 0) {
        return PASSPORT_APP_STORAGE_INVALID_PATH;
    }
    assert(s_job_count < MOCK_JOB_MAX);
    assert(app_id && strcmp(app_id, "test.storage") == 0);
    mock_job_t *job = &s_jobs[s_job_count++];
    memset(job, 0, sizeof(*job));
    job->operation = operation;
    job->request_id = request_id;
    snprintf(job->app_id, sizeof(job->app_id), "%s", app_id);
    snprintf(job->path, sizeof(job->path), "%s", path ? path : "");
    assert(data_size <= sizeof(job->data));
    if (data_size) memcpy(job->data, data, data_size);
    job->data_size = data_size;
    job->callback = callback;
    job->user = user;
    return PASSPORT_APP_STORAGE_OK;
}

passport_app_storage_error_t passport_app_storage_read_async(
    const char *app_id, const char *path, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    return submit_mock(PASSPORT_APP_STORAGE_READ, app_id, path, NULL, 0U,
                       request_id, callback, user);
}

passport_app_storage_error_t passport_app_storage_write_async(
    const char *app_id, const char *path, const void *data, size_t size,
    uint32_t request_id, passport_app_storage_completion_cb_t callback,
    void *user)
{
    return submit_mock(PASSPORT_APP_STORAGE_WRITE, app_id, path, data, size,
                       request_id, callback, user);
}

passport_app_storage_error_t passport_app_storage_remove_async(
    const char *app_id, const char *path, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    return submit_mock(PASSPORT_APP_STORAGE_REMOVE, app_id, path, NULL, 0U,
                       request_id, callback, user);
}

passport_app_storage_error_t passport_app_storage_list_async(
    const char *app_id, const char *path, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    return submit_mock(PASSPORT_APP_STORAGE_LIST, app_id, path, NULL, 0U,
                       request_id, callback, user);
}

passport_app_storage_error_t passport_app_storage_usage_async(
    const char *app_id, uint32_t request_id,
    passport_app_storage_completion_cb_t callback, void *user)
{
    return submit_mock(PASSPORT_APP_STORAGE_USAGE, app_id, "", NULL, 0U,
                       request_id, callback, user);
}

void passport_app_storage_completion_free(
    passport_app_storage_completion_t *completion)
{
    if (!completion) return;
    ++s_completion_free_count;
    free(completion->data);
    free(completion->entries);
    free(completion);
}

static void capture_dispatch(passport_app_storage_completion_t *completion,
                             void *user)
{
    (void)user;
    assert(!s_dispatched);
    s_dispatched = completion;
}

static passport_app_storage_completion_t *complete_job(
    size_t index, passport_app_storage_error_t error)
{
    assert(index < s_job_count);
    mock_job_t *job = &s_jobs[index];
    passport_app_storage_completion_t *completion = calloc(1, sizeof(*completion));
    assert(completion);
    completion->operation = job->operation;
    completion->error = error;
    completion->request_id = job->request_id;
    snprintf(completion->app_id, sizeof(completion->app_id), "%s", job->app_id);
    job->callback(completion, job->user);
    assert(s_dispatched == completion);
    s_dispatched = NULL;
    return completion;
}

static void run_lua(lua_State *L, const char *source)
{
    int status = luaL_dostring(L, source);
    if (status != LUA_OK) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
    }
    assert(status == LUA_OK);
}

int main(void)
{
    lua_State *L = luaL_newstate();
    assert(L);
    luaL_openlibs(L);

    passport_runtime_storage_state_t state;
    passport_runtime_storage_start(&state, L, "test.storage");
    passport_runtime_storage_set_dispatcher(capture_dispatch, NULL);
    passport_runtime_storage_register(L, &state);
    lua_setglobal(L, "storage");

    run_lua(L,
        "assert(storage.Error.OK == 0)\n"
        "assert(storage.Error.BUSY == 6)\n"
        "read_id, read_submit = storage.read('state.bin', function(error, data)\n"
        "  read_error, read_data = error, data\n"
        "end)\n"
        "assert(read_id and read_submit == storage.Error.OK)\n"
        "assert(read_error == nil)");
    assert(s_job_count == 1U);
    assert(s_jobs[0].operation == PASSPORT_APP_STORAGE_READ);
    assert(strcmp(s_jobs[0].path, "state.bin") == 0);
    passport_app_storage_completion_t *completion = complete_job(
        0U, PASSPORT_APP_STORAGE_OK);
    const uint8_t binary[] = {'A', 0U, 'B'};
    completion->data = malloc(sizeof(binary));
    assert(completion->data);
    memcpy(completion->data, binary, sizeof(binary));
    completion->data_size = sizeof(binary);
    passport_runtime_storage_handle_completion(&state, completion);
    run_lua(L,
        "assert(read_error == storage.Error.OK)\n"
        "assert(#read_data == 3 and string.byte(read_data, 1) == 65 and "
        "string.byte(read_data, 2) == 0 and string.byte(read_data, 3) == 66)");

    run_lua(L,
        "write_id, write_submit = storage.write('state.bin', 'x\\0y', "
        "function(error) write_error = error end)\n"
        "assert(write_id and write_submit == storage.Error.OK)");
    assert(s_job_count == 2U);
    assert(s_jobs[1].operation == PASSPORT_APP_STORAGE_WRITE);
    assert(s_jobs[1].data_size == 3U);
    assert(memcmp(s_jobs[1].data, "x\0y", 3U) == 0);
    completion = complete_job(1U, PASSPORT_APP_STORAGE_OK);
    passport_runtime_storage_handle_completion(&state, completion);
    run_lua(L, "assert(write_error == storage.Error.OK)");

    run_lua(L,
        "list_id, list_submit = storage.list(function(error, entries)\n"
        "  list_error, list_entries = error, entries\n"
        "end)\n"
        "assert(list_id and list_submit == storage.Error.OK)");
    assert(s_jobs[2].operation == PASSPORT_APP_STORAGE_LIST);
    assert(s_jobs[2].path[0] == '\0');
    completion = complete_job(2U, PASSPORT_APP_STORAGE_OK);
    completion->entries = calloc(2U, sizeof(*completion->entries));
    assert(completion->entries);
    completion->entry_count = 2U;
    snprintf(completion->entries[0].name, sizeof(completion->entries[0].name),
             "folder");
    completion->entries[0].is_directory = true;
    snprintf(completion->entries[1].name, sizeof(completion->entries[1].name),
             "state.bin");
    completion->entries[1].size = 3U;
    passport_runtime_storage_handle_completion(&state, completion);
    run_lua(L,
        "assert(list_error == storage.Error.OK and #list_entries == 2)\n"
        "assert(list_entries[1].name == 'folder' and "
        "list_entries[1].is_directory and list_entries[1].size == 0)\n"
        "assert(list_entries[2].name == 'state.bin' and "
        "not list_entries[2].is_directory and list_entries[2].size == 3)");

    run_lua(L,
        "usage_id, usage_submit = storage.usage(function(error, used, quota, files)\n"
        "  usage_error, usage_used, usage_quota, usage_files = "
        "error, used, quota, files\n"
        "end)\n"
        "assert(usage_id and usage_submit == storage.Error.OK)");
    completion = complete_job(3U, PASSPORT_APP_STORAGE_OK);
    completion->used_bytes = 4096U;
    completion->quota_bytes = PASSPORT_APP_STORAGE_QUOTA_BYTES;
    completion->file_count = 1U;
    passport_runtime_storage_handle_completion(&state, completion);
    run_lua(L,
        "assert(usage_error == storage.Error.OK and usage_used == 4096)\n"
        "assert(usage_quota == 65536 and usage_files == 1)");

    run_lua(L,
        "first_id = storage.remove('one', function(error) first_error = error end)\n"
        "second_id = storage.read('two', function(error, data) "
        "second_error, second_data = error, data end)\n"
        "third_id, third_error = storage.usage(function() error('must not run') end)\n"
        "assert(first_id and second_id and third_id == nil)\n"
        "assert(third_error == storage.Error.BUSY)");
    assert(s_job_count == 6U);
    completion = complete_job(4U, PASSPORT_APP_STORAGE_OK);
    passport_runtime_storage_handle_completion(&state, completion);
    completion = complete_job(5U, PASSPORT_APP_STORAGE_NOT_FOUND);
    passport_runtime_storage_handle_completion(&state, completion);
    run_lua(L,
        "assert(first_error == storage.Error.OK)\n"
        "assert(second_error == storage.Error.NOT_FOUND and second_data == nil)");

    run_lua(L,
        "bad_id, bad_error = storage.read('../bad', function() error('must not run') end)\n"
        "assert(bad_id == nil and bad_error == storage.Error.INVALID_PATH)\n"
        "final_id, final_submit = storage.usage(function() final_callback = true end)\n"
        "assert(final_id and final_submit == storage.Error.OK)");
    assert(s_job_count == 7U);
    passport_runtime_storage_stop(&state);
    completion = complete_job(6U, PASSPORT_APP_STORAGE_OK);
    passport_runtime_storage_handle_completion(&state, completion);
    run_lua(L, "assert(final_callback == nil)");

    assert(s_completion_free_count == 7U);
    lua_close(L);
    puts("Passport Lua storage API host tests: PASS");
    return 0;
}
