// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "denon_avr" /* Stable catalog and NVS identity. */

typedef struct {
    const char *action;
    const char *main_command;
    const char *zone2_command;
    const char *zone3_command;
} command_map_t;

typedef struct {
    const char *name;
    const char *wire;
} value_map_t;

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
    {"menu_on", "Menu On", "navigation"},
    {"menu_off", "Menu Off", "navigation"},
    {"cursor_up", "Cursor Up", "navigation"},
    {"cursor_down", "Cursor Down", "navigation"},
    {"cursor_left", "Cursor Left", "navigation"},
    {"cursor_right", "Cursor Right", "navigation"},
    {"enter", "Enter", "navigation"},
    {"return", "Return", "navigation"},
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
        .label = "TCP port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 23,
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
    {"power_on", "PWON", "Z2ON", "Z3ON"},
    {"standby", "PWSTANDBY", "Z2OFF", "Z3OFF"},
    {"power_off", "PWSTANDBY", "Z2OFF", "Z3OFF"},
    {"power_status", "PW?", "Z2?", "Z3?"},
    {"status_query", "PW?", "Z2?", "Z3?"},
    {"volume_up", "MVUP", "Z2UP", "Z3UP"},
    {"volume_down", "MVDOWN", "Z2DOWN", "Z3DOWN"},
    {"volume_status", "MV?", "Z2?", "Z3?"},
    {"mute_on", "MUON", "Z2MUON", "Z3MUON"},
    {"mute_off", "MUOFF", "Z2MUOFF", "Z3MUOFF"},
    {"mute_status", "MU?", "Z2MU?", "Z3MU?"},
    {"menu_on", "MNMEN ON", 0, 0},
    {"menu_off", "MNMEN OFF", 0, 0},
    {"cursor_up", "MNCUP", 0, 0},
    {"cursor_down", "MNCDN", 0, 0},
    {"cursor_left", "MNCLT", 0, 0},
    {"cursor_right", "MNCRT", 0, 0},
    {"enter", "MNENT", 0, 0},
    {"return", "MNRTN", 0, 0},
};

static const value_map_t s_inputs[] = {
    {"phono", "PHONO"}, {"cd", "CD"}, {"tuner", "TUNER"},
    {"dvd", "DVD"}, {"bd", "BD"}, {"tv", "TV"},
    {"sat", "SAT"}, {"sat_cbl", "SAT/CBL"}, {"game", "GAME"},
    {"media_player", "MPLAY"}, {"aux1", "AUX1"}, {"aux2", "AUX2"},
    {"net", "NET"}, {"network", "NET"}, {"usb", "USB"},
    {"spotify", "SPOTIFY"}, {"bluetooth", "BT"},
};

static const value_map_t s_modes[] = {
    {"movie", "MOVIE"}, {"music", "MUSIC"}, {"game", "GAME"},
    {"direct", "DIRECT"}, {"pure_direct", "PURE DIRECT"},
    {"stereo", "STEREO"}, {"standard", "STANDARD"},
    {"dolby_digital", "DOLBY DIGITAL"}, {"dts_surround", "DTS SURROUND"},
    {"multi_ch_stereo", "MCH STEREO"},
};

static kw_plugin_status_t target(char host[64], uint32_t *port, char zone[9])
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_u32(s_host, PLUGIN_ID, "port", 23, 1, 65535, port);
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

static const char *mapped_value(const value_map_t *map,
                                size_t count,
                                const char *name)
{
    for (size_t i = 0; i < count; i++) {
        if (kw_string_equal(name, map[i].name)) return map[i].wire;
    }
    return 0;
}

static uint8_t protocol_value_valid(const char *value, size_t maximum)
{
    size_t length = 0;
    while (value && value[length]) {
        char c = value[length++];
        if (length >= maximum || c < 0x20 || c > 0x7e ||
            c == '\r' || c == '\n' || c == '?') {
            return 0;
        }
    }
    return length > 0;
}

static const char *fixed_command(const char *action, const char *zone)
{
    for (size_t i = 0; i < sizeof(s_commands) / sizeof(s_commands[0]); i++) {
        if (!kw_string_equal(action, s_commands[i].action)) continue;
        if (kw_string_equal(zone, "zone2")) return s_commands[i].zone2_command;
        if (kw_string_equal(zone, "zone3")) return s_commands[i].zone3_command;
        return s_commands[i].main_command;
    }
    return 0;
}

static uint8_t build_dynamic(const char *action,
                             const char *zone,
                             char *wire,
                             size_t wire_size)
{
    const char *value = 0;
    const char *prefix = 0;
    if (kw_string_starts_with(action, "input:")) {
        value = mapped_value(s_inputs, sizeof(s_inputs) / sizeof(s_inputs[0]),
                             action + 6);
        prefix = kw_string_equal(zone, "main") ? "SI" :
                 (kw_string_equal(zone, "zone2") ? "Z2" : "Z3");
    } else if (kw_string_starts_with(action, "sound_mode:") &&
               kw_string_equal(zone, "main")) {
        value = mapped_value(s_modes, sizeof(s_modes) / sizeof(s_modes[0]),
                             action + 11);
        prefix = "MS";
    } else if (kw_string_starts_with(action, "volume:")) {
        uint32_t volume = 0;
        if (!kw_parse_u32(action + 7, 0, 98, &volume)) return 0;
        size_t offset = 0;
        prefix = kw_string_equal(zone, "main") ? "MV" :
                 (kw_string_equal(zone, "zone2") ? "Z2" : "Z3");
        wire[0] = '\0';
        return kw_append(wire, wire_size, &offset, prefix) &&
               kw_append_u32(wire, wire_size, &offset, volume);
    } else if (kw_string_starts_with(action, "quick_select:")) {
        const char *slot = action + 13;
        if (!slot[0] || slot[1] || slot[0] < '1' || slot[0] > '5') return 0;
        size_t offset = 0;
        wire[0] = '\0';
        if (kw_string_equal(zone, "main")) {
            return kw_append(wire, wire_size, &offset, "MSQUICK") &&
                   kw_append_char(wire, wire_size, &offset, slot[0]);
        }
        return kw_append(wire, wire_size, &offset,
                         kw_string_equal(zone, "zone2") ? "Z2QUICK" :
                         "Z3QUICK") &&
               kw_append_char(wire, wire_size, &offset, slot[0]);
    }
    if (!value || !prefix || !protocol_value_valid(value, 32)) return 0;
    size_t offset = 0;
    wire[0] = '\0';
    return kw_append(wire, wire_size, &offset, prefix) &&
           kw_append(wire, wire_size, &offset, value);
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

    char wire[128];
    const char *fixed = fixed_command(action, zone);
    if (fixed) {
        if (!kw_copy(wire, sizeof(wire), fixed)) {
            return KW_PLUGIN_STATUS_UNSUPPORTED;
        }
    } else if (!build_dynamic(action, zone, wire, sizeof(wire))) {
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
    .required_abi_minor = 4u,
    .id = PLUGIN_ID,
    .display_name = "Denon / Marantz AVR",
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
    .discovery_profile = "denon_avr",
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
