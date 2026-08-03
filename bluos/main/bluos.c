// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "bluos"

typedef struct {
    const char *name;
    const char *path;
} bluos_action_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"play", "Play", "playback"},
    {"pause", "Pause", "playback"},
    {"skip", "Skip", "playback"},
    {"back", "Back", "playback"},
    {"optical", "Optical", "source"},
    {"bluetooth", "Bluetooth", "source"},
    {"spotify", "Spotify", "source"},
    {"stream", "Stream", "source"},
    {"vol_up", "Volume Up", "volume"},
    {"vol_down", "Volume Down", "volume"},
};

static const bluos_action_t s_requests[] = {
    {"play", "/Play"},
    {"pause", "/Pause"},
    {"skip", "/Skip"},
    {"back", "/Back"},
    {"optical", "/Preset?id=1"},
    {"bluetooth", "/Preset?id=2"},
    {"spotify", "/Preset?id=3"},
    {"stream", "/Preset?id=3"},
    {"vol_up", "/Volume?level=+2"},
    {"vol_down", "/Volume?level=-2"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "BluOS IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 63,
    },
    {
        .key = "port",
        .label = "HTTP port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 11000,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
};

static kw_plugin_status_t target(char host[64], uint32_t *port)
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return kw_setting_u32(
        s_host, PLUGIN_ID, "port", 11000, 1, 65535, port);
}

static kw_plugin_status_t bind_host(const kw_plugin_host_api_v1_t *host)
{
    if (!host || host->abi_major != KW_PLUGIN_ABI_MAJOR ||
        host->abi_minor < 2u ||
        host->struct_size < offsetof(kw_plugin_host_api_v1_t, tcp_exchange) ||
        !host->http_request ||
        !host->setting_get_string || !host->setting_get_u32) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    s_host = host;
    return KW_PLUGIN_STATUS_OK;
}

static int32_t initialize(void)
{
    return s_host ? 0 : -1;
}

static int32_t start(void)
{
    s_started = 1;
    return 0;
}

static void stop(void)
{
    s_started = 0;
}

static void deinitialize(void)
{
    s_started = 0;
}

static kw_plugin_status_t validate_config(void)
{
    char host[64];
    uint32_t port;
    return target(host, &port);
}

static kw_plugin_status_t execute_action(const char *action)
{
    const char *path = 0;
    for (size_t i = 0; i < sizeof(s_requests) / sizeof(s_requests[0]); i++) {
        if (kw_string_equal(action, s_requests[i].name)) {
            path = s_requests[i].path;
            break;
        }
    }
    if (!path) return KW_PLUGIN_STATUS_UNSUPPORTED;

    char host[64];
    uint32_t port;
    kw_plugin_status_t result = target(host, &port);
    if (result != KW_PLUGIN_STATUS_OK) return result;

    kw_plugin_http_request_v1_t request;
    kw_http_request_init(&request);
    request.method = KW_PLUGIN_HTTP_GET;
    request.host = host;
    request.port = (uint16_t)port;
    request.path = path;
    return s_host->http_request(&request);
}

static char *status_json(void)
{
    return 0;
}

static kw_plugin_connection_t connection_state(void)
{
    char host[64];
    uint32_t port;
    return s_started && target(host, &port) == KW_PLUGIN_STATUS_OK
        ? KW_PLUGIN_CONNECTION_CONNECTED
        : KW_PLUGIN_CONNECTION_DISCONNECTED;
}

static const kw_plugin_descriptor_v1_t s_descriptor = {
    .magic = KW_PLUGIN_ABI_MAGIC,
    .struct_size = sizeof(kw_plugin_descriptor_v1_t),
    .required_abi_major = KW_PLUGIN_ABI_MAJOR,
    .required_abi_minor = 2u,
    .id = PLUGIN_ID,
    .display_name = "BluOS",
    .version = "0.1.1",
    .tier = KW_PLUGIN_TIER_PREVIEW,
    .bind = bind_host,
    .initialize = initialize,
    .start = start,
    .stop = stop,
    .deinitialize = deinitialize,
    .validate_config = validate_config,
    .execute_action = execute_action,
    .get_status_json = status_json,
    .get_connection_state = connection_state,
    .actions = s_actions,
    .action_count = sizeof(s_actions) / sizeof(s_actions[0]),
    .settings = s_settings,
    .setting_count = sizeof(s_settings) / sizeof(s_settings[0]),
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
