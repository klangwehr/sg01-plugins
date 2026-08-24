// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "spotify"

typedef struct {
    const char *action;
    kw_plugin_http_method_t method;
    const char *path;
    uint8_t has_query;
} action_map_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"play", "Play", "playback"},
    {"pause", "Pause", "playback"},
    {"skip", "Skip", "playback"},
    {"next", "Next", "playback"},
    {"back", "Back", "playback"},
    {"prev", "Previous", "playback"},
    {"shuffle", "Shuffle", "playback"},
    {"repeat", "Repeat", "playback"},
    {"vol_up", "Volume Up", "volume"},
    {"vol_down", "Volume Down", "volume"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "client_id",
        .label = "Spotify client ID",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "",
        .maximum_length = 127,
    },
    {
        .key = "client_secret",
        .label = "Spotify client secret",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 127,
    },
    {
        .key = "refresh_token",
        .label = "OAuth refresh token",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 511,
    },
    {
        .key = "access_token",
        .label = "OAuth access token",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 1024,
    },
    {
        .key = "device_id",
        .label = "Spotify Connect device ID",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "",
        .maximum_length = 127,
    },
};

static const action_map_t s_action_map[] = {
    {"play", KW_PLUGIN_HTTP_PUT, "/v1/me/player/play", 0},
    {"pause", KW_PLUGIN_HTTP_PUT, "/v1/me/player/pause", 0},
    {"skip", KW_PLUGIN_HTTP_POST, "/v1/me/player/next", 0},
    {"next", KW_PLUGIN_HTTP_POST, "/v1/me/player/next", 0},
    {"back", KW_PLUGIN_HTTP_POST, "/v1/me/player/previous", 0},
    {"prev", KW_PLUGIN_HTTP_POST, "/v1/me/player/previous", 0},
    {"shuffle", KW_PLUGIN_HTTP_PUT,
     "/v1/me/player/shuffle?state=true", 1},
    {"repeat", KW_PLUGIN_HTTP_PUT,
     "/v1/me/player/repeat?state=context", 1},
    {"vol_up", KW_PLUGIN_HTTP_PUT,
     "/v1/me/player/volume?volume_percent=70", 1},
    {"vol_down", KW_PLUGIN_HTTP_PUT,
     "/v1/me/player/volume?volume_percent=30", 1},
};

static uint8_t extract_access_token(const char *json,
                                    char *token,
                                    size_t token_size)
{
    const char marker[] = "\"access_token\":\"";
    for (size_t i = 0; json && json[i]; i++) {
        size_t j = 0;
        while (marker[j] && json[i + j] == marker[j]) j++;
        if (marker[j]) continue;
        size_t start = i + j;
        size_t length = 0;
        while (json[start + length] &&
               json[start + length] != '"' &&
               length + 1u < token_size) {
            char c = json[start + length];
            if (c < 0x21 || c > 0x7e || c == '\\') return 0;
            token[length++] = c;
        }
        if (!length || json[start + length] != '"') return 0;
        token[length] = '\0';
        return 1;
    }
    return 0;
}

static uint8_t form_append_encoded(char *output,
                                   size_t output_size,
                                   size_t *offset,
                                   const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    while (value && *value) {
        uint8_t c = (uint8_t)*value++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            if (!kw_append_char(
                    output, output_size, offset, (char)c)) return 0;
        } else {
            if (!kw_append_char(output, output_size, offset, '%') ||
                !kw_append_char(
                    output, output_size, offset, hex[c >> 4u]) ||
                !kw_append_char(
                    output, output_size, offset, hex[c & 0x0fu])) {
                return 0;
            }
        }
    }
    return 1;
}

static kw_plugin_status_t refresh_access_token(char token[1025])
{
    char client_id[128], client_secret[128], refresh_token[512];
    kw_plugin_status_t result = kw_setting_string(
        s_host, PLUGIN_ID, "client_id", client_id, sizeof(client_id), 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "client_secret", client_secret,
        sizeof(client_secret), 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "refresh_token", refresh_token,
        sizeof(refresh_token), 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;

    char authorization[352];
    if (!kw_basic_authorization(
            client_id, client_secret, authorization,
            sizeof(authorization))) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    char body[640];
    size_t body_length = 0;
    body[0] = '\0';
    if (!kw_append(
            body, sizeof(body), &body_length,
            "grant_type=refresh_token&refresh_token=") ||
        !form_append_encoded(
            body, sizeof(body), &body_length, refresh_token)) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    kw_plugin_http_header_v1_t headers[] = {
        {"Authorization", authorization},
    };
    char response[1536];
    size_t response_length = 0;
    kw_plugin_http_request_v1_t request;
    kw_http_request_init(&request);
    request.method = KW_PLUGIN_HTTP_POST;
    request.use_tls = 1;
    request.host = "accounts.spotify.com";
    request.port = 443;
    request.path = "/api/token";
    request.content_type = "application/x-www-form-urlencoded";
    request.body = body;
    request.body_length = body_length;
    request.headers = headers;
    request.header_count = 1;
    request.response = response;
    request.response_size = sizeof(response);
    request.response_length = &response_length;
    result = s_host->http_request(&request);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!extract_access_token(response, token, 1025)) {
        return KW_PLUGIN_STATUS_AUTH_FAILED;
    }
    return s_host->setting_set_string(
        PLUGIN_ID, "access_token", token);
}

static kw_plugin_status_t get_access_token(char token[1025],
                                           uint8_t allow_refresh)
{
    kw_plugin_status_t result = kw_setting_string(
        s_host, PLUGIN_ID, "access_token", token, 1025, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (token[0]) return KW_PLUGIN_STATUS_OK;
    return allow_refresh
        ? refresh_access_token(token)
        : KW_PLUGIN_STATUS_NOT_CONFIGURED;
}

static uint8_t build_path(const action_map_t *mapping,
                          const char *device_id,
                          char *path,
                          size_t path_size)
{
    size_t offset = 0;
    path[0] = '\0';
    if (!kw_append(path, path_size, &offset, mapping->path)) return 0;
    if (!device_id[0]) return 1;
    return kw_append_char(
               path, path_size, &offset,
               mapping->has_query ? '&' : '?') &&
           kw_append(path, path_size, &offset, "device_id=") &&
           kw_append(path, path_size, &offset, device_id);
}

static kw_plugin_status_t api_request(const action_map_t *mapping,
                                      const char *device_id,
                                      char token[1025])
{
    char path[256];
    if (!build_path(mapping, device_id, path, sizeof(path))) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    char authorization[1050];
    size_t auth_length = 0;
    authorization[0] = '\0';
    if (!kw_append(
            authorization, sizeof(authorization), &auth_length, "Bearer ") ||
        !kw_append(
            authorization, sizeof(authorization), &auth_length, token)) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    kw_plugin_http_header_v1_t header = {
        "Authorization", authorization,
    };
    kw_plugin_http_request_v1_t request;
    kw_http_request_init(&request);
    request.method = mapping->method;
    request.use_tls = 1;
    request.host = "api.spotify.com";
    request.port = 443;
    request.path = path;
    request.headers = &header;
    request.header_count = 1;
    return s_host->http_request(&request);
}

static const action_map_t *mapping_for_action(const char *action)
{
    for (size_t i = 0; i < sizeof(s_action_map) / sizeof(s_action_map[0]); i++) {
        if (kw_string_equal(action, s_action_map[i].action)) {
            return &s_action_map[i];
        }
    }
    return 0;
}

static kw_plugin_status_t bind_host(const kw_plugin_host_api_v1_t *host)
{
    if (!host || host->abi_major != 1u || host->abi_minor < 2u ||
        host->struct_size < offsetof(kw_plugin_host_api_v1_t, tcp_exchange) ||
        !host->http_request || !host->setting_get_string ||
        !host->setting_set_string) {
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
    char token[1025];
    kw_plugin_status_t result = get_access_token(token, 0);
    if (result == KW_PLUGIN_STATUS_OK) return result;
    char client_id[128], client_secret[128], refresh_token[512];
    if (kw_setting_string(
            s_host, PLUGIN_ID, "client_id", client_id,
            sizeof(client_id), 1) != KW_PLUGIN_STATUS_OK ||
        kw_setting_string(
            s_host, PLUGIN_ID, "client_secret", client_secret,
            sizeof(client_secret), 1) != KW_PLUGIN_STATUS_OK ||
        kw_setting_string(
            s_host, PLUGIN_ID, "refresh_token", refresh_token,
            sizeof(refresh_token), 1) != KW_PLUGIN_STATUS_OK) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    return KW_PLUGIN_STATUS_OK;
}

static kw_plugin_status_t execute_action(const char *action)
{
    const action_map_t *mapping = mapping_for_action(action);
    if (!mapping) return KW_PLUGIN_STATUS_UNSUPPORTED;
    char token[1025], device_id[128];
    kw_plugin_status_t result = get_access_token(token, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "device_id", device_id,
        sizeof(device_id), 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = api_request(mapping, device_id, token);
    if (result != KW_PLUGIN_STATUS_AUTH_FAILED) return result;
    result = refresh_access_token(token);
    return result == KW_PLUGIN_STATUS_OK
        ? api_request(mapping, device_id, token) : result;
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
    .display_name = "Spotify",
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
    .discovery_profile = "spotify",
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
