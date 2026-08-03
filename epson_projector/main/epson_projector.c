// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "epson_projector"

typedef struct {
    const char *action;
    const char *wire;
} command_map_t;

typedef struct {
    const char *name;
    const char *value;
} value_map_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"power_on", "Power On", "power"},
    {"power_off", "Power Off", "power"},
    {"standby", "Standby", "power"},
    {"power_status", "Power Status", "power"},
    {"status_query", "Status Query", "system"},
    {"mute_on", "Mute On", "mute"},
    {"mute_off", "Mute Off", "mute"},
    {"mute_status", "Mute Status", "mute"},
    {"freeze_on", "Freeze On", "display"},
    {"freeze_off", "Freeze Off", "display"},
    {"freeze_status", "Freeze Status", "display"},
    {"lamp_status", "Lamp Status", "system"},
    {"filter_status", "Filter Status", "system"},
    {"source_status", "Source Status", "source"},
    {"zoom_inc", "Zoom In", "lens"},
    {"zoom_dec", "Zoom Out", "lens"},
    {"focus_inc", "Focus In", "lens"},
    {"focus_dec", "Focus Out", "lens"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "Projector IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 63,
    },
    {
        .key = "port",
        .label = "ESC/VP21 TCP port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 3629,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
};

static const command_map_t s_commands[] = {
    {"power_on", "PWR ON"},
    {"power_off", "PWR OFF"},
    {"standby", "PWR OFF"},
    {"power_status", "PWR?"},
    {"status_query", "PWR?"},
    {"mute_on", "MUTE ON"},
    {"mute_off", "MUTE OFF"},
    {"mute_status", "MUTE?"},
    {"freeze_on", "FREEZE ON"},
    {"freeze_off", "FREEZE OFF"},
    {"freeze_status", "FREEZE?"},
    {"luminance:normal", "LUMINANCE 00"},
    {"luminance:eco", "LUMINANCE 01"},
    {"luminance_status", "LUMINANCE?"},
    {"lamp_status", "LAMP?"},
    {"filter_status", "FILTER?"},
    {"serial_status", "SNO?"},
    {"source_status", "SOURCE?"},
    {"input_status", "SOURCE?"},
    {"color_mode_status", "CMODE?"},
    {"zoom_min", "ZOOM MIN"},
    {"zoom_max", "ZOOM MAX"},
    {"zoom_inc", "ZOOM INC"},
    {"zoom_dec", "ZOOM DEC"},
    {"zoom_off", "ZOOM OFF"},
    {"focus_min", "FOCUS MIN"},
    {"focus_max", "FOCUS MAX"},
    {"focus_inc", "FOCUS INC"},
    {"focus_dec", "FOCUS DEC"},
    {"focus_off", "FOCUS OFF"},
};

static const value_map_t s_sources[] = {
    {"computer1", "11"}, {"computer2", "12"}, {"computer", "11"},
    {"hdmi", "30"}, {"hdmi1", "30"}, {"hdmi2", "A0"},
    {"video", "41"}, {"s_video", "42"}, {"usb_display", "51"},
    {"usb", "52"}, {"lan", "53"}, {"network", "53"},
    {"screen_mirroring", "56"},
};

static const value_map_t s_color_modes[] = {
    {"srgb", "01"}, {"normal", "02"}, {"meeting", "03"},
    {"presentation", "04"}, {"theatre", "05"}, {"theater", "05"},
    {"dynamic", "06"}, {"game", "06"}, {"sports", "08"},
    {"dicom", "0F"}, {"custom", "10"}, {"blackboard", "11"},
    {"whiteboard", "12"}, {"photo", "14"},
};

static kw_plugin_status_t target(char host[64], uint32_t *port)
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return kw_setting_u32(
        s_host, PLUGIN_ID, "port", 3629, 1, 65535, port);
}

static const char *mapped_value(const value_map_t *map,
                                size_t count,
                                const char *name)
{
    for (size_t i = 0; i < count; i++) {
        if (kw_string_equal(name, map[i].name)) return map[i].value;
    }
    return 0;
}

static const char *fixed_command(const char *action)
{
    for (size_t i = 0; i < sizeof(s_commands) / sizeof(s_commands[0]); i++) {
        if (kw_string_equal(action, s_commands[i].action)) {
            return s_commands[i].wire;
        }
    }
    return 0;
}

static uint8_t build_value_command(const char *prefix,
                                   const char *value,
                                   char *wire,
                                   size_t wire_size)
{
    size_t offset = 0;
    wire[0] = '\0';
    return value &&
           kw_append(wire, wire_size, &offset, prefix) &&
           kw_append_char(wire, wire_size, &offset, ' ') &&
           kw_append(wire, wire_size, &offset, value);
}

static uint8_t build_dynamic(const char *action,
                             char *wire,
                             size_t wire_size)
{
    if (kw_string_starts_with(action, "source:")) {
        return build_value_command(
            "SOURCE",
            mapped_value(s_sources, sizeof(s_sources) / sizeof(s_sources[0]),
                         action + 7),
            wire, wire_size);
    }
    if (kw_string_starts_with(action, "color_mode:")) {
        return build_value_command(
            "CMODE",
            mapped_value(s_color_modes,
                         sizeof(s_color_modes) / sizeof(s_color_modes[0]),
                         action + 11),
            wire, wire_size);
    }
    if (kw_string_starts_with(action, "volume:")) {
        uint32_t value = 0;
        if (!kw_parse_u32(action + 7, 0, 31, &value)) return 0;
        size_t offset = 0;
        wire[0] = '\0';
        return kw_append(wire, wire_size, &offset, "VOL ") &&
               kw_append_u32(wire, wire_size, &offset, value);
    }
    return 0;
}

static kw_plugin_status_t bind_host(const kw_plugin_host_api_v1_t *host)
{
    if (!host || host->abi_major != 1u || host->abi_minor < 3u ||
        host->struct_size < sizeof(*host) || !host->tcp_exchange ||
        !host->setting_get_string || !host->setting_get_u32) {
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
    uint32_t port;
    return target(host, &port);
}

static kw_plugin_status_t execute_action(const char *action)
{
    char host[64];
    uint32_t port;
    kw_plugin_status_t result = target(host, &port);
    if (result != KW_PLUGIN_STATUS_OK) return result;

    char wire[96];
    const char *fixed = fixed_command(action);
    if (fixed) {
        if (!kw_copy(wire, sizeof(wire), fixed)) {
            return KW_PLUGIN_STATUS_UNSUPPORTED;
        }
    } else if (!build_dynamic(action, wire, sizeof(wire))) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    size_t wire_length = kw_string_length(wire);
    if (!kw_append_char(wire, sizeof(wire), &wire_length, '\r')) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }

    uint8_t response[192];
    size_t response_length = 0;
    kw_plugin_tcp_request_v1_t request;
    kw_tcp_request_init(&request);
    request.host = host;
    request.port = (uint16_t)port;
    request.connect_timeout_ms = 3000;
    request.read_timeout_ms = 3000;
    request.write_data = (const uint8_t *)wire;
    request.write_length = wire_length;
    request.read_buffer = response;
    request.read_buffer_size = sizeof(response);
    request.read_length = &response_length;
    return s_host->tcp_exchange(&request);
}

static char *status_json(void) { return 0; }

static kw_plugin_connection_t connection_state(void)
{
    return s_started && validate_config() == KW_PLUGIN_STATUS_OK
        ? KW_PLUGIN_CONNECTION_CONNECTED
        : KW_PLUGIN_CONNECTION_DISCONNECTED;
}

static const kw_plugin_descriptor_v1_t s_descriptor = {
    .magic = KW_PLUGIN_ABI_MAGIC,
    .struct_size = sizeof(kw_plugin_descriptor_v1_t),
    .required_abi_major = 1u,
    .required_abi_minor = 3u,
    .id = PLUGIN_ID,
    .display_name = "Epson Projector",
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
