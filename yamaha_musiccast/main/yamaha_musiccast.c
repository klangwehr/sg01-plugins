// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "yamaha_musiccast"

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"power_on", "Power On", "power"},
    {"standby", "Standby", "power"},
    {"power_off", "Power Off", "power"},
    {"status_query", "Status Query", "system"},
    {"volume_up", "Volume Up", "volume"},
    {"volume_down", "Volume Down", "volume"},
    {"mute_on", "Mute On", "volume"},
    {"mute_off", "Mute Off", "volume"},
    {"play", "Play", "playback"},
    {"pause", "Pause", "playback"},
    {"stop", "Stop", "playback"},
    {"next", "Next", "playback"},
    {"previous", "Previous", "playback"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "MusicCast IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 63,
    },
    {
        .key = "port",
        .label = "HTTP port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 80,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
    {
        .key = "zone",
        .label = "Zone",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "main",
        .maximum_length = 11,
    },
    {
        .key = "volume_step",
        .label = "Volume step",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 10,
        .minimum_u32 = 1,
        .maximum_u32 = 100,
    },
};

static kw_plugin_status_t load_target(char host[64],
                                      uint32_t *port,
                                      char zone[12],
                                      uint32_t *step)
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_u32(
        s_host, PLUGIN_ID, "port", 80, 1, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "zone", zone, 12, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!zone[0]) kw_copy(zone, 12, "main");
    if (!kw_identifier_valid(zone, 0)) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    return kw_setting_u32(
        s_host, PLUGIN_ID, "volume_step", 10, 1, 100, step);
}

static uint8_t build_path(const char *action,
                          const char *zone,
                          uint32_t step,
                          char path[192])
{
    size_t offset = 0;
    path[0] = '\0';
    if (kw_string_equal(action, "status_query")) {
        return kw_append(path, 192, &offset,
            "/YamahaExtendedControl/v1/system/getDeviceInfo");
    }
    if (kw_string_equal(action, "play") ||
        kw_string_equal(action, "pause") ||
        kw_string_equal(action, "stop") ||
        kw_string_equal(action, "next") ||
        kw_string_equal(action, "previous")) {
        return kw_append(path, 192, &offset,
                    "/YamahaExtendedControl/v1/netusb/setPlayback?playback=") &&
               kw_append(path, 192, &offset, action);
    }
    if (!kw_append(path, 192, &offset,
                   "/YamahaExtendedControl/v1/") ||
        !kw_append(path, 192, &offset, zone) ||
        !kw_append_char(path, 192, &offset, '/')) {
        return 0;
    }
    if (kw_string_equal(action, "power_on")) {
        return kw_append(path, 192, &offset, "setPower?power=on");
    }
    if (kw_string_equal(action, "standby") ||
        kw_string_equal(action, "power_off")) {
        return kw_append(path, 192, &offset, "setPower?power=standby");
    }
    if (kw_string_equal(action, "mute_on")) {
        return kw_append(path, 192, &offset, "setMute?enable=true");
    }
    if (kw_string_equal(action, "mute_off")) {
        return kw_append(path, 192, &offset, "setMute?enable=false");
    }
    if (kw_string_equal(action, "volume_up") ||
        kw_string_equal(action, "volume_down")) {
        return kw_append(path, 192, &offset,
                         "setVolume?volume=") &&
               kw_append(path, 192, &offset,
                         kw_string_equal(action, "volume_up") ? "up" : "down") &&
               kw_append(path, 192, &offset, "&step=") &&
               kw_append_u32(path, 192, &offset, step);
    }
    if (kw_string_starts_with(action, "input:") &&
        kw_identifier_valid(action + 6, 0)) {
        return kw_append(path, 192, &offset, "setInput?input=") &&
               kw_append(path, 192, &offset, action + 6);
    }
    if (kw_string_starts_with(action, "playback:") &&
        kw_identifier_valid(action + 9, 0)) {
        offset = 0;
        path[0] = '\0';
        return kw_append(path, 192, &offset,
                    "/YamahaExtendedControl/v1/netusb/setPlayback?playback=") &&
               kw_append(path, 192, &offset, action + 9);
    }
    return 0;
}

static kw_plugin_status_t bind_host(const kw_plugin_host_api_v1_t *host)
{
    if (!host || host->abi_major != KW_PLUGIN_ABI_MAJOR ||
        host->abi_minor < 2u ||
        host->struct_size < offsetof(kw_plugin_host_api_v1_t, tcp_exchange) ||
        !host->http_request) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    s_host = host;
    return KW_PLUGIN_STATUS_OK;
}

static int32_t initialize(void) { return s_host ? 0 : -1; }
static int32_t start(void) { s_started = 1; return 0; }
static void stop(void) { s_started = 0; }
static void deinitialize(void) { s_started = 0; }

static kw_plugin_status_t validate_config(void)
{
    char host[64];
    char zone[12];
    uint32_t port;
    uint32_t step;
    return load_target(host, &port, zone, &step);
}

static kw_plugin_status_t execute_action(const char *action)
{
    char host[64];
    char zone[12];
    char path[192];
    uint32_t port;
    uint32_t step;
    kw_plugin_status_t result =
        load_target(host, &port, zone, &step);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!build_path(action, zone, step, path)) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    char response[128];
    size_t response_length = 0;
    kw_plugin_http_request_v1_t request;
    kw_http_request_init(&request);
    request.method = KW_PLUGIN_HTTP_GET;
    request.host = host;
    request.port = (uint16_t)port;
    request.path = path;
    request.response = response;
    request.response_size = sizeof(response);
    request.response_length = &response_length;
    return s_host->http_request(&request);
}

static char *status_json(void) { return 0; }

static kw_plugin_connection_t connection_state(void)
{
    return s_started ? KW_PLUGIN_CONNECTION_CONNECTED
                     : KW_PLUGIN_CONNECTION_DISCONNECTED;
}

static const kw_plugin_descriptor_v1_t s_descriptor = {
    .magic = KW_PLUGIN_ABI_MAGIC,
    .struct_size = sizeof(kw_plugin_descriptor_v1_t),
    .required_abi_major = KW_PLUGIN_ABI_MAJOR,
    .required_abi_minor = 4u,
    .id = PLUGIN_ID,
    .display_name = "Yamaha MusicCast",
    .version = "0.1.2",
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
    .discovery_profile = "yamaha_musiccast",
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
