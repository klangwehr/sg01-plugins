// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define ROKU_PLUGIN_ID "roku"
#define ROKU_DEFAULT_PORT 8060u
#define ROKU_TIMEOUT_MS 3000u

typedef struct {
    const char *action;
    const char *key;
} roku_action_map_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"home",         "Home",         "navigation"},
    {"back",         "Back",         "navigation"},
    {"select",       "Select",       "navigation"},
    {"up",           "Up",           "navigation"},
    {"down",         "Down",         "navigation"},
    {"left",         "Left",         "navigation"},
    {"right",        "Right",        "navigation"},
    {"play_pause",   "Play/Pause",   "playback"},
    {"rewind",       "Rewind",       "playback"},
    {"fast_forward", "Fast Forward", "playback"},
    {"volume_up",    "Volume Up",    "volume"},
    {"volume_down",  "Volume Down",  "volume"},
    {"mute",         "Mute",         "volume"},
    {"power",        "Power",        "power"},
    {"power_on",     "Power On",     "power"},
    {"power_off",    "Power Off",    "power"},
};

static const roku_action_map_t s_action_map[] = {
    {"home", "Home"},
    {"back", "Back"},
    {"select", "Select"},
    {"up", "Up"},
    {"down", "Down"},
    {"left", "Left"},
    {"right", "Right"},
    {"play_pause", "Play"},
    {"rewind", "Rev"},
    {"fast_forward", "Fwd"},
    {"volume_up", "VolumeUp"},
    {"volume_down", "VolumeDown"},
    {"mute", "VolumeMute"},
    {"power", "Power"},
    {"power_on", "PowerOn"},
    {"power_off", "PowerOff"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "Roku IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 63,
    },
    {
        .key = "port",
        .label = "ECP port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = ROKU_DEFAULT_PORT,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
};

static uint8_t strings_equal(const char *left, const char *right)
{
    if (!left || !right) return 0;
    while (*left && *right && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static const char *action_key(const char *action)
{
    uint32_t count =
        (uint32_t)(sizeof(s_action_map) / sizeof(s_action_map[0]));
    for (uint32_t i = 0; i < count; i++) {
        if (strings_equal(action, s_action_map[i].action)) {
            return s_action_map[i].key;
        }
    }
    return 0;
}

static uint8_t append_string(char *output,
                             uint32_t output_size,
                             uint32_t *offset,
                             const char *value)
{
    while (value && *value) {
        if (*offset + 1u >= output_size) return 0;
        output[*offset] = *value;
        (*offset)++;
        value++;
    }
    output[*offset] = '\0';
    return 1;
}

static kw_plugin_status_t load_target(char host[64], uint32_t *port)
{
    if (!s_host || !s_host->setting_get_string ||
        !s_host->setting_get_u32) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    host[0] = '\0';
    if (s_host->setting_get_string(
            ROKU_PLUGIN_ID, "host", host, 64) != KW_PLUGIN_STATUS_OK ||
        host[0] == '\0') {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    *port = ROKU_DEFAULT_PORT;
    kw_plugin_status_t port_result = s_host->setting_get_u32(
        ROKU_PLUGIN_ID, "port", port);
    if (port_result == KW_PLUGIN_STATUS_NOT_CONFIGURED) {
        *port = ROKU_DEFAULT_PORT;
    } else if (port_result != KW_PLUGIN_STATUS_OK) {
        return port_result;
    }
    if (*port == 0 || *port > 65535u) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    return KW_PLUGIN_STATUS_OK;
}

static kw_plugin_status_t roku_bind(const kw_plugin_host_api_v1_t *host)
{
    if (!host || host->abi_major != KW_PLUGIN_ABI_MAJOR ||
        host->abi_minor < 2u ||
        host->struct_size < offsetof(kw_plugin_host_api_v1_t, tcp_exchange) ||
        !host->log || !host->http_request) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    s_host = host;
    return KW_PLUGIN_STATUS_OK;
}

static int32_t roku_initialize(void)
{
    if (!s_host) return -1;
    s_host->log(KW_PLUGIN_LOG_INFO, ROKU_PLUGIN_ID, "initialized");
    return 0;
}

static int32_t roku_start(void)
{
    if (!s_host) return -1;
    s_started = 1;
    s_host->log(KW_PLUGIN_LOG_INFO, ROKU_PLUGIN_ID, "started");
    return 0;
}

static void roku_stop(void)
{
    s_started = 0;
    if (s_host) {
        s_host->log(KW_PLUGIN_LOG_INFO, ROKU_PLUGIN_ID, "stopped");
    }
}

static void roku_deinitialize(void)
{
    s_started = 0;
}

static kw_plugin_status_t roku_validate_config(void)
{
    char host[64];
    uint32_t port;
    return load_target(host, &port);
}

static kw_plugin_status_t roku_execute_action(const char *action)
{
    const char *key = action_key(action);
    if (!key) return KW_PLUGIN_STATUS_UNSUPPORTED;

    char host[64];
    uint32_t port;
    kw_plugin_status_t target_result = load_target(host, &port);
    if (target_result != KW_PLUGIN_STATUS_OK) return target_result;

    char path[96];
    uint32_t offset = 0;
    path[0] = '\0';
    if (!append_string(path, sizeof(path), &offset, "/keypress/") ||
        !append_string(path, sizeof(path), &offset, key)) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    int32_t status_code = 0;
    kw_plugin_http_request_v1_t request = {0};
    request.struct_size = sizeof(kw_plugin_http_request_v1_t);
    request.method = KW_PLUGIN_HTTP_POST;
    request.use_tls = 0;
    request.reserved[0] = 0;
    request.reserved[1] = 0;
    request.reserved[2] = 0;
    request.host = host;
    request.port = (uint16_t)port;
    request.reserved_port = 0;
    request.path = path;
    request.content_type = 0;
    request.body = 0;
    request.body_length = 0;
    request.timeout_ms = ROKU_TIMEOUT_MS;
    request.expected_status_min = 200;
    request.expected_status_max = 300;
    request.response = 0;
    request.response_size = 0;
    request.response_length = 0;
    request.status_code = &status_code;
    return s_host->http_request(&request);
}

static char *roku_get_status_json(void)
{
    return 0;
}

static kw_plugin_connection_t roku_connection_state(void)
{
    char host[64];
    uint32_t port;
    if (!s_started || load_target(host, &port) != KW_PLUGIN_STATUS_OK) {
        return KW_PLUGIN_CONNECTION_DISCONNECTED;
    }
    return KW_PLUGIN_CONNECTION_CONNECTED;
}

static const kw_plugin_descriptor_v1_t s_descriptor = {
    .magic = KW_PLUGIN_ABI_MAGIC,
    .struct_size = sizeof(kw_plugin_descriptor_v1_t),
    .required_abi_major = KW_PLUGIN_ABI_MAJOR,
    .required_abi_minor = 2u,
    .id = ROKU_PLUGIN_ID,
    .display_name = "Roku",
    .version = "0.1.1",
    .tier = KW_PLUGIN_TIER_PREVIEW,
    .bind = roku_bind,
    .initialize = roku_initialize,
    .start = roku_start,
    .stop = roku_stop,
    .deinitialize = roku_deinitialize,
    .validate_config = roku_validate_config,
    .execute_action = roku_execute_action,
    .get_status_json = roku_get_status_json,
    .get_connection_state = roku_connection_state,
    .actions = s_actions,
    .action_count = sizeof(s_actions) / sizeof(s_actions[0]),
    .settings = s_settings,
    .setting_count = sizeof(s_settings) / sizeof(s_settings[0]),
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
