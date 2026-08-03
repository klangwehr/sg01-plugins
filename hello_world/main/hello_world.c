// SPDX-License-Identifier: Apache-2.0

#include "kw_plugin_abi.h"

static const kw_plugin_host_api_v1_t *s_host;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {
        .name = "say_hello",
        .label = "Say hello",
        .category = "diagnostics",
    },
};

static kw_plugin_status_t hello_bind(const kw_plugin_host_api_v1_t *host)
{
    if (!host || host->abi_major != KW_PLUGIN_ABI_MAJOR ||
        host->abi_minor < 2u ||
        host->struct_size < offsetof(kw_plugin_host_api_v1_t, tcp_exchange)) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    s_host = host;
    return KW_PLUGIN_STATUS_OK;
}

static int32_t hello_initialize(void)
{
    if (!s_host) return -1;
    s_host->log(KW_PLUGIN_LOG_INFO, "hello_world", "initialized");
    return 0;
}

static int32_t hello_start(void)
{
    s_host->log(KW_PLUGIN_LOG_INFO, "hello_world", "started");
    return 0;
}

static void hello_stop(void)
{
    if (s_host) s_host->log(KW_PLUGIN_LOG_INFO, "hello_world", "stopped");
}

static void hello_deinitialize(void)
{
    /* bind() is once per load; the host table remains valid across a
     * disable/re-enable cycle and until the module is unloaded. */
}

static kw_plugin_status_t hello_validate_config(void)
{
    return KW_PLUGIN_STATUS_OK;
}

static kw_plugin_status_t hello_execute_action(const char *action)
{
    if (!action || !s_host) return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    s_host->log(KW_PLUGIN_LOG_INFO, "hello_world", "hello from an ELF module");
    return KW_PLUGIN_STATUS_OK;
}

static char *hello_get_status_json(void)
{
    return 0;
}

static kw_plugin_connection_t hello_connection_state(void)
{
    return s_host ? KW_PLUGIN_CONNECTION_CONNECTED
                  : KW_PLUGIN_CONNECTION_DISCONNECTED;
}

static const kw_plugin_descriptor_v1_t s_descriptor = {
    .magic = KW_PLUGIN_ABI_MAGIC,
    .struct_size = sizeof(kw_plugin_descriptor_v1_t),
    .required_abi_major = KW_PLUGIN_ABI_MAJOR,
    .required_abi_minor = 2u,
    .id = "hello_world",
    .display_name = "Hello World",
    .version = "0.1.0",
    .tier = KW_PLUGIN_TIER_PREVIEW,
    .flags = 0,
    .bind = hello_bind,
    .initialize = hello_initialize,
    .start = hello_start,
    .stop = hello_stop,
    .deinitialize = hello_deinitialize,
    .validate_config = hello_validate_config,
    .execute_action = hello_execute_action,
    .get_status_json = hello_get_status_json,
    .get_connection_state = hello_connection_state,
    .actions = s_actions,
    .action_count = sizeof(s_actions) / sizeof(s_actions[0]),
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
