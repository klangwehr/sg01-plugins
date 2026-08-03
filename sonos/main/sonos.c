// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "sonos"

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"play", "Play", "playback"},
    {"pause", "Pause", "playback"},
    {"next", "Next", "playback"},
    {"previous", "Previous", "playback"},
    {"volume_up", "Volume Up", "volume"},
    {"volume_down", "Volume Down", "volume"},
    {"mute_on", "Mute On", "volume"},
    {"mute_off", "Mute Off", "volume"},
    {"favorite", "Favorite", "playback"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "Sonos API hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "api.ws.sonos.com",
        .maximum_length = 95,
    },
    {
        .key = "port",
        .label = "HTTPS port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 443,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
    {
        .key = "access_token",
        .label = "Sonos access token",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED | KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 1024,
    },
    {
        .key = "group_id",
        .label = "Sonos group ID",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 95,
    },
    {
        .key = "favorite_id",
        .label = "Default favorite ID",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "",
        .maximum_length = 95,
    },
    {
        .key = "timeout_ms",
        .label = "Request timeout (ms)",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 4000,
        .minimum_u32 = 500,
        .maximum_u32 = 30000,
    },
};

static uint8_t resource_id_valid(const char *value)
{
    if (!value || !value[0]) return 0;
    while (*value) {
        char c = *value++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' ||
            c == ':' || c == '.') {
            continue;
        }
        return 0;
    }
    return 1;
}

static kw_plugin_status_t load_config(char host[96],
                                      uint32_t *port,
                                      char token[1025],
                                      char group[96],
                                      char favorite[96],
                                      uint32_t *timeout)
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 96, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!host[0]) kw_copy(host, 96, "api.ws.sonos.com");
    result = kw_setting_u32(
        s_host, PLUGIN_ID, "port", 443, 1, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "access_token", token, 1025, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "group_id", group, 96, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!resource_id_valid(group)) return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "favorite_id", favorite, 96, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (favorite[0] && !resource_id_valid(favorite)) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    return kw_setting_u32(
        s_host, PLUGIN_ID, "timeout_ms", 4000, 500, 30000, timeout);
}

static uint8_t action_request(const char *action,
                              const char *group,
                              const char *default_favorite,
                              char path[256],
                              char body[192])
{
    const char *suffix = 0;
    const char *favorite = default_favorite;
    body[0] = '\0';
    if (kw_string_equal(action, "play")) suffix = "playback/play";
    else if (kw_string_equal(action, "pause")) suffix = "playback/pause";
    else if (kw_string_equal(action, "next")) {
        suffix = "playback/skipToNextTrack";
    } else if (kw_string_equal(action, "previous") ||
               kw_string_equal(action, "prev")) {
        suffix = "playback/skipToPreviousTrack";
    } else if (kw_string_equal(action, "volume_up") ||
               kw_string_equal(action, "vol_up")) {
        suffix = "groupVolume/relative";
        kw_copy(body, 192, "{\"volumeDelta\":5}");
    } else if (kw_string_equal(action, "volume_down") ||
               kw_string_equal(action, "vol_down")) {
        suffix = "groupVolume/relative";
        kw_copy(body, 192, "{\"volumeDelta\":-5}");
    } else if (kw_string_equal(action, "mute_on")) {
        suffix = "groupVolume/mute";
        kw_copy(body, 192, "{\"muted\":true}");
    } else if (kw_string_equal(action, "mute_off")) {
        suffix = "groupVolume/mute";
        kw_copy(body, 192, "{\"muted\":false}");
    } else if (kw_string_starts_with(action, "favorite:")) {
        suffix = "favorites";
        favorite = action + 9;
    } else if (kw_string_starts_with(action, "preset:")) {
        suffix = "favorites";
        favorite = action + 7;
    } else if (kw_string_equal(action, "favorite") ||
               kw_string_equal(action, "preset")) {
        suffix = "favorites";
    } else {
        return 0;
    }

    if (kw_string_equal(suffix, "favorites")) {
        if (!resource_id_valid(favorite)) return 0;
        size_t body_offset = 0;
        if (!kw_append(body, 192, &body_offset, "{\"favoriteId\":\"") ||
            !kw_append(body, 192, &body_offset, favorite) ||
            !kw_append(body, 192, &body_offset,
                "\",\"action\":\"PLAY_NOW\",\"playOnCompletion\":true}")) {
            return 0;
        }
    }

    size_t offset = 0;
    path[0] = '\0';
    return kw_append(path, 256, &offset, "/control/api/v1/groups/") &&
           kw_append(path, 256, &offset, group) &&
           kw_append_char(path, 256, &offset, '/') &&
           kw_append(path, 256, &offset, suffix);
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
    char host[96], token[1025], group[96], favorite[96];
    uint32_t port, timeout;
    return load_config(
        host, &port, token, group, favorite, &timeout);
}

static kw_plugin_status_t execute_action(const char *action)
{
    char host[96], token[1025], group[96], favorite[96];
    char path[256], body[192], authorization[1040];
    uint32_t port, timeout;
    kw_plugin_status_t result = load_config(
        host, &port, token, group, favorite, &timeout);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!action_request(action, group, favorite, path, body)) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    size_t auth_offset = 0;
    authorization[0] = '\0';
    if (!kw_append(authorization, sizeof(authorization), &auth_offset,
                   "Bearer ") ||
        !kw_append(authorization, sizeof(authorization), &auth_offset,
                   token)) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    const kw_plugin_http_header_v1_t headers[] = {
        {"Authorization", authorization},
        {"User-Agent", "Klangwehr-Signalgeraet/sonos-module"},
    };
    kw_plugin_http_request_v1_t request;
    kw_http_request_init(&request);
    request.method = KW_PLUGIN_HTTP_POST;
    request.use_tls = 1;
    request.host = host;
    request.port = (uint16_t)port;
    request.path = path;
    request.content_type = "application/json";
    request.body = body;
    request.body_length = kw_string_length(body);
    request.timeout_ms = timeout;
    request.headers = headers;
    request.header_count = sizeof(headers) / sizeof(headers[0]);
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
    .required_abi_minor = 2u,
    .id = PLUGIN_ID,
    .display_name = "Sonos",
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
