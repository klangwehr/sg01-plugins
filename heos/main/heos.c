// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "heos"

typedef struct {
    const char *action;
    const char *command;
    uint8_t needs_player;
} action_map_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"play", "Play", "playback"},
    {"pause", "Pause", "playback"},
    {"stop", "Stop", "playback"},
    {"next", "Next", "playback"},
    {"previous", "Previous", "playback"},
    {"volume_up", "Volume Up", "volume"},
    {"volume_down", "Volume Down", "volume"},
    {"volume_status", "Volume Status", "volume"},
    {"mute_on", "Mute On", "volume"},
    {"mute_off", "Mute Off", "volume"},
    {"mute_status", "Mute Status", "volume"},
    {"toggle_mute", "Toggle Mute", "volume"},
    {"now_playing", "Now Playing", "playback"},
    {"play_state", "Play State", "playback"},
    {"players", "Get Players", "system"},
    {"status_query", "Status Query", "system"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "HEOS device IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 63,
    },
    {
        .key = "port",
        .label = "CLI port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 1255,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
    {
        .key = "player_id",
        .label = "Player ID",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "",
        .maximum_length = 31,
    },
    {
        .key = "volume_step",
        .label = "Volume step",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 5,
        .minimum_u32 = 1,
        .maximum_u32 = 20,
    },
};

static const action_map_t s_action_map[] = {
    {"players", "player/get_players", 0},
    {"get_players", "player/get_players", 0},
    {"now_playing", "player/get_now_playing_media", 1},
    {"play_state", "player/get_play_state", 1},
    {"volume_status", "player/get_volume", 1},
    {"mute_status", "player/get_mute", 1},
    {"toggle_mute", "player/toggle_mute", 1},
    {"next", "player/play_next", 1},
    {"previous", "player/play_previous", 1},
};

static uint8_t player_id_valid(const char *value)
{
    if (!value || !value[0] || kw_string_length(value) > 31) return 0;
    while (*value) {
        char c = *value++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            continue;
        }
        return 0;
    }
    return 1;
}

static kw_plugin_status_t target(char host[64],
                                 uint32_t *port,
                                 char player_id[32],
                                 uint32_t *volume_step)
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result =
        kw_setting_u32(s_host, PLUGIN_ID, "port", 1255, 1, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "player_id", player_id, 32, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (player_id[0] && !player_id_valid(player_id)) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    return kw_setting_u32(
        s_host, PLUGIN_ID, "volume_step", 5, 1, 20, volume_step);
}

static uint8_t append_player(char *wire,
                             size_t wire_size,
                             size_t *offset,
                             const char *player_id)
{
    return player_id_valid(player_id) &&
           kw_append(wire, wire_size, offset, "?pid=") &&
           kw_append(wire, wire_size, offset, player_id);
}

static uint8_t build_command(const char *action,
                             const char *player_id,
                             uint32_t volume_step,
                             char *wire,
                             size_t wire_size)
{
    size_t offset = 0;
    wire[0] = '\0';
    if (!kw_append(wire, wire_size, &offset, "heos://")) return 0;

    if (kw_string_equal(action, "status_query")) {
        if (player_id_valid(player_id)) {
            return kw_append(wire, wire_size, &offset,
                             "player/get_play_state") &&
                   append_player(wire, wire_size, &offset, player_id);
        }
        return kw_append(wire, wire_size, &offset, "player/get_players");
    }
    if (kw_string_equal(action, "play") ||
        kw_string_equal(action, "pause") ||
        kw_string_equal(action, "stop")) {
        return kw_append(wire, wire_size, &offset,
                         "player/set_play_state") &&
               append_player(wire, wire_size, &offset, player_id) &&
               kw_append(wire, wire_size, &offset, "&state=") &&
               kw_append(wire, wire_size, &offset, action);
    }
    if (kw_string_equal(action, "volume_up") ||
        kw_string_equal(action, "volume_down")) {
        return kw_append(wire, wire_size, &offset, "player/") &&
               kw_append(wire, wire_size, &offset, action) &&
               append_player(wire, wire_size, &offset, player_id) &&
               kw_append(wire, wire_size, &offset, "&step=") &&
               kw_append_u32(wire, wire_size, &offset, volume_step);
    }
    if (kw_string_equal(action, "mute_on") ||
        kw_string_equal(action, "mute_off")) {
        return kw_append(wire, wire_size, &offset, "player/set_mute") &&
               append_player(wire, wire_size, &offset, player_id) &&
               kw_append(wire, wire_size, &offset, "&state=") &&
               kw_append(wire, wire_size, &offset,
                         kw_string_equal(action, "mute_on") ? "on" : "off");
    }
    if (kw_string_starts_with(action, "volume:")) {
        uint32_t volume = 0;
        if (!kw_parse_u32(action + 7, 0, 100, &volume)) return 0;
        return kw_append(wire, wire_size, &offset, "player/set_volume") &&
               append_player(wire, wire_size, &offset, player_id) &&
               kw_append(wire, wire_size, &offset, "&level=") &&
               kw_append_u32(wire, wire_size, &offset, volume);
    }
    if (kw_string_starts_with(action, "quick_select:")) {
        const char *slot = action + 13;
        if (!slot[0] || slot[1] || slot[0] < '1' || slot[0] > '6') return 0;
        return kw_append(wire, wire_size, &offset,
                         "player/play_quickselect") &&
               append_player(wire, wire_size, &offset, player_id) &&
               kw_append(wire, wire_size, &offset, "&quick_select=") &&
               kw_append_char(wire, wire_size, &offset, slot[0]);
    }
    for (size_t i = 0; i < sizeof(s_action_map) / sizeof(s_action_map[0]); i++) {
        if (!kw_string_equal(action, s_action_map[i].action)) continue;
        if (!kw_append(wire, wire_size, &offset, s_action_map[i].command)) {
            return 0;
        }
        return !s_action_map[i].needs_player ||
               append_player(wire, wire_size, &offset, player_id);
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
    char player_id[32];
    uint32_t port;
    uint32_t volume_step;
    return target(host, &port, player_id, &volume_step);
}

static kw_plugin_status_t execute_action(const char *action)
{
    char host[64];
    char player_id[32];
    uint32_t port;
    uint32_t volume_step;
    kw_plugin_status_t result =
        target(host, &port, player_id, &volume_step);
    if (result != KW_PLUGIN_STATUS_OK) return result;

    char wire[224];
    if (!build_command(action, player_id, volume_step, wire, sizeof(wire))) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    size_t wire_length = kw_string_length(wire);
    if (!kw_append_char(wire, sizeof(wire), &wire_length, '\r') ||
        !kw_append_char(wire, sizeof(wire), &wire_length, '\n')) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }

    uint8_t response[768];
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
    .display_name = "HEOS",
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
    .discovery_profile = "heos",
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
