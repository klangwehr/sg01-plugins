// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "eiscp"

typedef struct {
    const char *action;
    const char *main_prefix;
    const char *zone2_prefix;
    const char *zone3_prefix;
    const char *parameter;
} command_map_t;

typedef struct {
    const char *name;
    const char *code;
} input_map_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"power_on", "Power On", "power"},
    {"standby", "Standby", "power"},
    {"power_off", "Power Off", "power"},
    {"power_status", "Power Status", "power"},
    {"status_query", "Status Query", "system"},
    {"volume_up", "Volume Up", "volume"},
    {"volume_down", "Volume Down", "volume"},
    {"volume_status", "Volume Status", "volume"},
    {"mute_on", "Mute On", "volume"},
    {"mute_off", "Mute Off", "volume"},
    {"mute_status", "Mute Status", "volume"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "Receiver IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 63,
    },
    {
        .key = "port",
        .label = "eISCP TCP port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 60128,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
    {
        .key = "zone",
        .label = "Zone (main, zone2, or zone3)",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "main",
        .maximum_length = 8,
    },
};

static const command_map_t s_commands[] = {
    {"power_on", "PWR", "ZPW", "PW3", "01"},
    {"standby", "PWR", "ZPW", "PW3", "00"},
    {"power_off", "PWR", "ZPW", "PW3", "00"},
    {"power_status", "PWR", "ZPW", "PW3", "QSTN"},
    {"status_query", "PWR", "ZPW", "PW3", "QSTN"},
    {"volume_up", "MVL", "ZVL", "VL3", "UP"},
    {"volume_down", "MVL", "ZVL", "VL3", "DOWN"},
    {"volume_status", "MVL", "ZVL", "VL3", "QSTN"},
    {"mute_on", "AMT", "ZMT", "MT3", "01"},
    {"mute_off", "AMT", "ZMT", "MT3", "00"},
    {"mute_status", "AMT", "ZMT", "MT3", "QSTN"},
    {"input_status", "SLI", "SLZ", "SL3", "QSTN"},
};

static const input_map_t s_inputs[] = {
    {"cbl_sat", "01"}, {"sat_cbl", "01"}, {"sat", "01"},
    {"game", "02"}, {"aux", "03"}, {"pc", "05"},
    {"bd", "10"}, {"dvd", "10"}, {"tv", "23"}, {"cd", "23"},
    {"phono", "22"}, {"fm", "24"}, {"am", "25"},
    {"tuner", "24"}, {"net", "2B"}, {"network", "2B"},
    {"usb", "2C"}, {"bluetooth", "2E"}, {"bt", "2E"},
};

static kw_plugin_status_t target(char host[64], uint32_t *port, char zone[9])
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result =
        kw_setting_u32(s_host, PLUGIN_ID, "port", 60128, 1, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(s_host, PLUGIN_ID, "zone", zone, 9, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!zone[0]) kw_copy(zone, 9, "main");
    if (!kw_string_equal(zone, "main") &&
        !kw_string_equal(zone, "zone2") &&
        !kw_string_equal(zone, "zone3")) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    return KW_PLUGIN_STATUS_OK;
}

static const char *input_code(const char *name)
{
    for (size_t i = 0; i < sizeof(s_inputs) / sizeof(s_inputs[0]); i++) {
        if (kw_string_equal(name, s_inputs[i].name)) return s_inputs[i].code;
    }
    return 0;
}

static const char *zone_prefix(const command_map_t *command, const char *zone)
{
    if (kw_string_equal(zone, "zone2")) return command->zone2_prefix;
    if (kw_string_equal(zone, "zone3")) return command->zone3_prefix;
    return command->main_prefix;
}

static uint8_t append_hex_byte(char *wire,
                               size_t wire_size,
                               size_t *offset,
                               uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    return kw_append_char(wire, wire_size, offset, hex[(value >> 4u) & 0xfu]) &&
           kw_append_char(wire, wire_size, offset, hex[value & 0xfu]);
}

static uint8_t build_command(const char *action,
                             const char *zone,
                             char *command,
                             size_t command_size)
{
    const char *prefix = 0;
    const char *parameter = 0;
    for (size_t i = 0; i < sizeof(s_commands) / sizeof(s_commands[0]); i++) {
        if (kw_string_equal(action, s_commands[i].action)) {
            prefix = zone_prefix(&s_commands[i], zone);
            parameter = s_commands[i].parameter;
            break;
        }
    }
    if (!prefix && kw_string_starts_with(action, "input:")) {
        parameter = input_code(action + 6);
        prefix = kw_string_equal(zone, "main") ? "SLI" :
                 (kw_string_equal(zone, "zone2") ? "SLZ" : "SL3");
    }

    size_t offset = 0;
    command[0] = '\0';
    if (prefix && parameter) {
        return kw_append(command, command_size, &offset, "!1") &&
               kw_append(command, command_size, &offset, prefix) &&
               kw_append(command, command_size, &offset, parameter);
    }
    if (kw_string_starts_with(action, "volume:")) {
        uint32_t volume = 0;
        if (!kw_parse_u32(action + 7, 0, 100, &volume)) return 0;
        prefix = kw_string_equal(zone, "main") ? "MVL" :
                 (kw_string_equal(zone, "zone2") ? "ZVL" : "VL3");
        return kw_append(command, command_size, &offset, "!1") &&
               kw_append(command, command_size, &offset, prefix) &&
               append_hex_byte(command, command_size, &offset, volume);
    }
    return 0;
}

static void put_be32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24u);
    output[1] = (uint8_t)(value >> 16u);
    output[2] = (uint8_t)(value >> 8u);
    output[3] = (uint8_t)value;
}

static size_t build_frame(const char *command,
                          uint8_t *frame,
                          size_t frame_size)
{
    size_t command_length = kw_string_length(command);
    size_t data_length = command_length + 1u;
    size_t total_length = 16u + data_length;
    if (!command_length || total_length > frame_size) return 0;
    for (size_t i = 0; i < total_length; i++) frame[i] = 0;
    frame[0] = 'I';
    frame[1] = 'S';
    frame[2] = 'C';
    frame[3] = 'P';
    put_be32(frame + 4, 16u);
    put_be32(frame + 8, (uint32_t)data_length);
    frame[12] = 1;
    for (size_t i = 0; i < command_length; i++) {
        frame[16 + i] = (uint8_t)command[i];
    }
    frame[16 + command_length] = '\r';
    return total_length;
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
    char zone[9];
    uint32_t port;
    return target(host, &port, zone);
}

static kw_plugin_status_t execute_action(const char *action)
{
    char host[64];
    char zone[9];
    uint32_t port;
    kw_plugin_status_t result = target(host, &port, zone);
    if (result != KW_PLUGIN_STATUS_OK) return result;

    char command[96];
    if (!build_command(action, zone, command, sizeof(command))) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    uint8_t frame[128];
    size_t frame_length = build_frame(command, frame, sizeof(frame));
    if (!frame_length) return KW_PLUGIN_STATUS_UNSUPPORTED;

    uint8_t response[256];
    size_t response_length = 0;
    kw_plugin_tcp_request_v1_t request;
    kw_tcp_request_init(&request);
    request.host = host;
    request.port = (uint16_t)port;
    request.connect_timeout_ms = 3000;
    request.read_timeout_ms = 3000;
    request.write_data = frame;
    request.write_length = frame_length;
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
    .display_name = "Onkyo / Integra eISCP",
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
