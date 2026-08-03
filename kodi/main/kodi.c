// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "kodi"

typedef struct {
    const char *action;
    const char *method;
    const char *params;
    uint8_t needs_player;
} kodi_action_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"ping", "Ping", "system"},
    {"up", "Up", "navigation"},
    {"down", "Down", "navigation"},
    {"left", "Left", "navigation"},
    {"right", "Right", "navigation"},
    {"select", "Select", "navigation"},
    {"back", "Back", "navigation"},
    {"home", "Home", "navigation"},
    {"context_menu", "Context Menu", "navigation"},
    {"play_pause", "Play/Pause", "playback"},
    {"stop", "Stop", "playback"},
    {"volume_up", "Volume Up", "volume"},
    {"volume_down", "Volume Down", "volume"},
    {"mute", "Mute", "volume"},
};

static const kodi_action_t s_requests[] = {
    {"ping", "JSONRPC.Ping", 0, 0},
    {"up", "Input.Up", 0, 0},
    {"down", "Input.Down", 0, 0},
    {"left", "Input.Left", 0, 0},
    {"right", "Input.Right", 0, 0},
    {"select", "Input.Select", 0, 0},
    {"back", "Input.Back", 0, 0},
    {"home", "Input.Home", 0, 0},
    {"context_menu", "Input.ContextMenu", 0, 0},
    {"play_pause", "Player.PlayPause", 0, 1},
    {"stop", "Player.Stop", 0, 1},
    {"volume_up", "Application.SetVolume",
        "{\"volume\":\"increment\"}", 0},
    {"volume_down", "Application.SetVolume",
        "{\"volume\":\"decrement\"}", 0},
    {"mute", "Application.SetMute", "{\"mute\":\"toggle\"}", 0},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "Kodi IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 95,
    },
    {
        .key = "port",
        .label = "Web server port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 8080,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
    {
        .key = "use_tls",
        .label = "Use HTTPS",
        .type = KW_PLUGIN_SETTING_BOOL,
        .default_u32 = 0,
        .minimum_u32 = 0,
        .maximum_u32 = 1,
    },
    {
        .key = "username",
        .label = "Username",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "",
        .maximum_length = 95,
    },
    {
        .key = "password",
        .label = "Password",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 95,
    },
};

static kw_plugin_status_t load_config(char host[96],
                                      uint32_t *port,
                                      uint32_t *use_tls,
                                      char username[96],
                                      char password[96])
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 96, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_u32(
        s_host, PLUGIN_ID, "port", 8080, 1, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_u32(
        s_host, PLUGIN_ID, "use_tls", 0, 0, 1, use_tls);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "username", username, 96, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return kw_setting_string(
        s_host, PLUGIN_ID, "password", password, 96, 0);
}

static uint8_t contains(const char *text, const char *needle)
{
    if (!text || !needle || !needle[0]) return 0;
    for (; *text; text++) {
        const char *left = text;
        const char *right = needle;
        while (*left && *right && *left == *right) {
            left++;
            right++;
        }
        if (!*right) return 1;
    }
    return 0;
}

static int32_t player_id(const char *json)
{
    static const char marker[] = "\"playerid\":";
    if (!json) return -1;
    for (const char *cursor = json; *cursor; cursor++) {
        const char *left = cursor;
        const char *right = marker;
        while (*left && *right && *left == *right) {
            left++;
            right++;
        }
        if (*right) continue;
        uint32_t value = 0;
        uint8_t found = 0;
        while (*left == ' ' || *left == '\t') left++;
        while (*left >= '0' && *left <= '9') {
            found = 1;
            value = value * 10u + (uint32_t)(*left - '0');
            if (value > 255u) return -1;
            left++;
        }
        return found ? (int32_t)value : -1;
    }
    return -1;
}

static kw_plugin_status_t rpc(const char *host,
                              uint32_t port,
                              uint32_t use_tls,
                              const char *authorization,
                              const char *method,
                              const char *params,
                              char *response,
                              size_t response_size)
{
    char body[320];
    size_t offset = 0;
    body[0] = '\0';
    if (!kw_append(body, sizeof(body), &offset,
                   "{\"jsonrpc\":\"2.0\",\"method\":\"") ||
        !kw_append(body, sizeof(body), &offset, method) ||
        !kw_append_char(body, sizeof(body), &offset, '"')) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    if (params &&
        (!kw_append(body, sizeof(body), &offset, ",\"params\":") ||
         !kw_append(body, sizeof(body), &offset, params))) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    if (!kw_append(body, sizeof(body), &offset, ",\"id\":1}")) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }

    kw_plugin_http_header_v1_t header;
    header.name = "Authorization";
    header.value = authorization;
    size_t response_length = 0;
    kw_plugin_http_request_v1_t request;
    kw_http_request_init(&request);
    request.method = KW_PLUGIN_HTTP_POST;
    request.use_tls = use_tls != 0;
    request.host = host;
    request.port = (uint16_t)port;
    request.path = "/jsonrpc";
    request.content_type = "application/json";
    request.body = body;
    request.body_length = offset;
    request.response = response;
    request.response_size = response_size;
    request.response_length = response ? &response_length : 0;
    request.headers = authorization && authorization[0] ? &header : 0;
    request.header_count = authorization && authorization[0] ? 1 : 0;
    kw_plugin_status_t result = s_host->http_request(&request);
    if (result == KW_PLUGIN_STATUS_OK && response &&
        contains(response, "\"error\"")) {
        return KW_PLUGIN_STATUS_DEVICE_ERROR;
    }
    return result;
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
    char host[96], username[96], password[96];
    uint32_t port, use_tls;
    return load_config(host, &port, &use_tls, username, password);
}

static kw_plugin_status_t execute_action(const char *action)
{
    const kodi_action_t *selected = 0;
    for (size_t i = 0; i < sizeof(s_requests) / sizeof(s_requests[0]); i++) {
        if (kw_string_equal(action, s_requests[i].action)) {
            selected = &s_requests[i];
            break;
        }
    }
    if (!selected) return KW_PLUGIN_STATUS_UNSUPPORTED;

    char host[96], username[96], password[96], authorization[288];
    uint32_t port, use_tls;
    kw_plugin_status_t result =
        load_config(host, &port, &use_tls, username, password);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    authorization[0] = '\0';
    if ((username[0] || password[0]) &&
        !kw_basic_authorization(
            username, password, authorization, sizeof(authorization))) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }

    char response[512];
    const char *params = selected->params;
    char player_params[32];
    if (selected->needs_player) {
        result = rpc(host, port, use_tls, authorization,
                     "Player.GetActivePlayers", 0,
                     response, sizeof(response));
        if (result != KW_PLUGIN_STATUS_OK) return result;
        int32_t id = player_id(response);
        if (id < 0) return KW_PLUGIN_STATUS_DEVICE_ERROR;
        size_t offset = 0;
        player_params[0] = '\0';
        if (!kw_append(player_params, sizeof(player_params), &offset,
                       "{\"playerid\":") ||
            !kw_append_u32(player_params, sizeof(player_params), &offset,
                           (uint32_t)id) ||
            !kw_append_char(player_params, sizeof(player_params), &offset,
                            '}')) {
            return KW_PLUGIN_STATUS_INTERNAL_ERROR;
        }
        params = player_params;
    }
    return rpc(host, port, use_tls, authorization,
               selected->method, params, response, sizeof(response));
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
    .required_abi_minor = 2u,
    .id = PLUGIN_ID,
    .display_name = "Kodi",
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
