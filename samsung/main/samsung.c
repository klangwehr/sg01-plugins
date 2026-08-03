// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "samsung"

typedef struct {
    const char *action;
    const char *key;
    const char *label;
    const char *category;
} action_map_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const action_map_t s_action_map[] = {
    {"power", "KEY_POWER", "Power", "power"},
    {"up", "KEY_UP", "Up", "navigation"},
    {"down", "KEY_DOWN", "Down", "navigation"},
    {"left", "KEY_LEFT", "Left", "navigation"},
    {"right", "KEY_RIGHT", "Right", "navigation"},
    {"enter", "KEY_ENTER", "Enter", "navigation"},
    {"return", "KEY_RETURN", "Return", "navigation"},
    {"home", "KEY_HOME", "Home", "navigation"},
    {"menu", "KEY_MENU", "Menu", "navigation"},
    {"source", "KEY_SOURCE", "Source", "navigation"},
    {"guide", "KEY_GUIDE", "Guide", "navigation"},
    {"info", "KEY_INFO", "Info", "navigation"},
    {"tools", "KEY_TOOLS", "Tools", "navigation"},
    {"exit", "KEY_EXIT", "Exit", "navigation"},
    {"vol_up", "KEY_VOLUP", "Volume Up", "volume"},
    {"vol_down", "KEY_VOLDOWN", "Volume Down", "volume"},
    {"mute", "KEY_MUTE", "Mute", "volume"},
    {"ch_up", "KEY_CHUP", "Channel Up", "channel"},
    {"ch_down", "KEY_CHDOWN", "Channel Down", "channel"},
    {"channel_list", "KEY_CH_LIST", "Channel List", "channel"},
    {"play", "KEY_PLAY", "Play", "playback"},
    {"pause", "KEY_PAUSE", "Pause", "playback"},
    {"stop", "KEY_STOP", "Stop", "playback"},
    {"rewind", "KEY_REWIND", "Rewind", "playback"},
    {"fast_forward", "KEY_FF", "Fast Forward", "playback"},
    {"record", "KEY_REC", "Record", "playback"},
    {"red", "KEY_RED", "Red", "colour"},
    {"green", "KEY_GREEN", "Green", "colour"},
    {"yellow", "KEY_YELLOW", "Yellow", "colour"},
    {"blue", "KEY_BLUE", "Blue", "colour"},
    {"source_tv", "KEY_TV", "Source TV", "source"},
    {"source_hdmi", "KEY_HDMI", "Source HDMI", "source"},
    {"source_hdmi1", "KEY_HDMI1", "Source HDMI 1", "source"},
    {"source_hdmi2", "KEY_HDMI2", "Source HDMI 2", "source"},
    {"source_hdmi3", "KEY_HDMI3", "Source HDMI 3", "source"},
    {"source_hdmi4", "KEY_HDMI4", "Source HDMI 4", "source"},
    {"digit_0", "KEY_0", "0", "digit"},
    {"digit_1", "KEY_1", "1", "digit"},
    {"digit_2", "KEY_2", "2", "digit"},
    {"digit_3", "KEY_3", "3", "digit"},
    {"digit_4", "KEY_4", "4", "digit"},
    {"digit_5", "KEY_5", "5", "digit"},
    {"digit_6", "KEY_6", "6", "digit"},
    {"digit_7", "KEY_7", "7", "digit"},
    {"digit_8", "KEY_8", "8", "digit"},
    {"digit_9", "KEY_9", "9", "digit"},
    {"ambient", "KEY_AMBIENT", "Ambient", "power"},
};

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"power", "Power", "power"},
    {"up", "Up", "navigation"},
    {"down", "Down", "navigation"},
    {"left", "Left", "navigation"},
    {"right", "Right", "navigation"},
    {"enter", "Enter", "navigation"},
    {"return", "Return", "navigation"},
    {"home", "Home", "navigation"},
    {"menu", "Menu", "navigation"},
    {"source", "Source", "navigation"},
    {"guide", "Guide", "navigation"},
    {"info", "Info", "navigation"},
    {"tools", "Tools", "navigation"},
    {"exit", "Exit", "navigation"},
    {"vol_up", "Volume Up", "volume"},
    {"vol_down", "Volume Down", "volume"},
    {"mute", "Mute", "volume"},
    {"ch_up", "Channel Up", "channel"},
    {"ch_down", "Channel Down", "channel"},
    {"channel_list", "Channel List", "channel"},
    {"play", "Play", "playback"},
    {"pause", "Pause", "playback"},
    {"stop", "Stop", "playback"},
    {"rewind", "Rewind", "playback"},
    {"fast_forward", "Fast Forward", "playback"},
    {"record", "Record", "playback"},
    {"red", "Red", "colour"},
    {"green", "Green", "colour"},
    {"yellow", "Yellow", "colour"},
    {"blue", "Blue", "colour"},
    {"source_tv", "Source TV", "source"},
    {"source_hdmi", "Source HDMI", "source"},
    {"source_hdmi1", "Source HDMI 1", "source"},
    {"source_hdmi2", "Source HDMI 2", "source"},
    {"source_hdmi3", "Source HDMI 3", "source"},
    {"source_hdmi4", "Source HDMI 4", "source"},
    {"digit_0", "0", "digit"},
    {"digit_1", "1", "digit"},
    {"digit_2", "2", "digit"},
    {"digit_3", "3", "digit"},
    {"digit_4", "4", "digit"},
    {"digit_5", "5", "digit"},
    {"digit_6", "6", "digit"},
    {"digit_7", "7", "digit"},
    {"digit_8", "8", "digit"},
    {"digit_9", "9", "digit"},
    {"ambient", "Ambient", "power"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "TV IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 63,
    },
    {
        .key = "port",
        .label = "WebSocket port (0 = automatic)",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 0,
        .minimum_u32 = 0,
        .maximum_u32 = 65535,
    },
    {
        .key = "scheme",
        .label = "Transport (auto, ws, or wss)",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "auto",
        .maximum_length = 4,
    },
    {
        .key = "device_name",
        .label = "Remote name shown on TV",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "signalgeraet",
        .maximum_length = 63,
    },
    {
        .key = "token",
        .label = "Pairing token",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 127,
    },
    {
        .key = "tls_skip",
        .label = "Allow self-signed TV certificate",
        .type = KW_PLUGIN_SETTING_BOOL,
        .default_u32 = 1,
        .minimum_u32 = 0,
        .maximum_u32 = 1,
    },
};

static uint8_t text_contains(const char *value, const char *needle)
{
    if (!value || !needle || !needle[0]) return 0;
    for (size_t i = 0; value[i]; i++) {
        size_t j = 0;
        while (needle[j] && value[i + j] == needle[j]) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}

static uint8_t extract_token(const char *response,
                             char *token,
                             size_t token_size)
{
    const char marker[] = "\"token\":\"";
    if (!response || !token || token_size < 2) return 0;
    for (size_t i = 0; response[i]; i++) {
        size_t j = 0;
        while (marker[j] && response[i + j] == marker[j]) j++;
        if (marker[j]) continue;
        size_t start = i + j;
        size_t length = 0;
        while (response[start + length] &&
               response[start + length] != '"' &&
               length + 1u < token_size) {
            char c = response[start + length];
            if (!((c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-')) {
                return 0;
            }
            token[length++] = c;
        }
        if (!length || response[start + length] != '"') return 0;
        token[length] = '\0';
        return 1;
    }
    return 0;
}

static const char *action_key(const char *action)
{
    for (size_t i = 0; i < sizeof(s_action_map) / sizeof(s_action_map[0]); i++) {
        if (kw_string_equal(action, s_action_map[i].action)) {
            return s_action_map[i].key;
        }
    }
    return 0;
}

static kw_plugin_status_t target(char host[64],
                                 uint32_t *port,
                                 char scheme[5],
                                 char device_name[64],
                                 char token[128],
                                 uint32_t *tls_skip)
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_u32(s_host, PLUGIN_ID, "port", 0, 0, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "scheme", scheme, 5, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!scheme[0]) kw_copy(scheme, 5, "auto");
    if (!kw_string_equal(scheme, "auto") &&
        !kw_string_equal(scheme, "ws") &&
        !kw_string_equal(scheme, "wss")) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    result = kw_setting_string(
        s_host, PLUGIN_ID, "device_name", device_name, 64, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!device_name[0]) kw_copy(device_name, 64, "signalgeraet");
    result =
        kw_setting_string(s_host, PLUGIN_ID, "token", token, 128, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return kw_setting_u32(
        s_host, PLUGIN_ID, "tls_skip", 1, 0, 1, tls_skip);
}

static uint8_t build_url(char *url,
                         size_t url_size,
                         const char *host,
                         uint32_t configured_port,
                         uint8_t secure,
                         const char *device_name,
                         const char *token)
{
    char encoded_name[88];
    if (!kw_base64((const uint8_t *)device_name,
                   kw_string_length(device_name),
                   encoded_name, sizeof(encoded_name))) {
        return 0;
    }
    uint32_t port = configured_port ? configured_port : (secure ? 8002 : 8001);
    size_t offset = 0;
    url[0] = '\0';
    if (!kw_append(url, url_size, &offset, secure ? "wss://" : "ws://") ||
        !kw_append(url, url_size, &offset, host) ||
        !kw_append_char(url, url_size, &offset, ':') ||
        !kw_append_u32(url, url_size, &offset, port) ||
        !kw_append(url, url_size, &offset,
                   "/api/v2/channels/samsung.remote.control?name=") ||
        !kw_append(url, url_size, &offset, encoded_name)) {
        return 0;
    }
    return !token[0] ||
           (kw_append(url, url_size, &offset, "&token=") &&
            kw_append(url, url_size, &offset, token));
}

static uint8_t build_frame(char *frame,
                           size_t frame_size,
                           const char *key)
{
    size_t offset = 0;
    frame[0] = '\0';
    return kw_append(
               frame, frame_size, &offset,
               "{\"method\":\"ms.remote.control\",\"params\":"
               "{\"Cmd\":\"Click\",\"DataOfCmd\":\"") &&
           kw_append(frame, frame_size, &offset, key) &&
           kw_append(
               frame, frame_size, &offset,
               "\",\"Option\":\"false\","
               "\"TypeOfRemote\":\"SendRemoteKey\"}}");
}

static kw_plugin_status_t exchange(uint8_t secure,
                                   const char *host,
                                   uint32_t port,
                                   const char *device_name,
                                   const char *token,
                                   uint32_t tls_skip,
                                   const char *frame,
                                   char *handshake,
                                   size_t handshake_size)
{
    char url[512];
    if (!build_url(url, sizeof(url), host, port, secure,
                   device_name, token)) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    size_t handshake_length = 0;
    kw_plugin_websocket_frame_v1_t ws_frame = {
        .text = frame,
        .text_length = kw_string_length(frame),
    };
    kw_plugin_websocket_request_v1_t request = {0};
    request.struct_size = sizeof(request);
    request.uri = url;
    request.timeout_ms = 5000;
    request.skip_certificate_name_check = tls_skip != 0;
    request.initial_response = handshake;
    request.initial_response_size = handshake_size;
    request.initial_response_length = &handshake_length;
    request.frames = &ws_frame;
    request.frame_count = 1;
    return s_host->websocket_exchange(&request);
}

static kw_plugin_status_t bind_host(const kw_plugin_host_api_v1_t *host)
{
    if (!host || host->abi_major != 1u || host->abi_minor < 3u ||
        host->struct_size < sizeof(*host) || !host->websocket_exchange ||
        !host->setting_get_string || !host->setting_set_string ||
        !host->setting_get_u32) {
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
    char host[64], scheme[5], device_name[64], token[128];
    uint32_t port, tls_skip;
    return target(
        host, &port, scheme, device_name, token, &tls_skip);
}

static kw_plugin_status_t execute_action(const char *action)
{
    const char *key = action_key(action);
    if (!key) return KW_PLUGIN_STATUS_UNSUPPORTED;
    char frame[256];
    if (!build_frame(frame, sizeof(frame), key)) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    char host[64], scheme[5], device_name[64], token[128];
    uint32_t port, tls_skip;
    kw_plugin_status_t result = target(
        host, &port, scheme, device_name, token, &tls_skip);
    if (result != KW_PLUGIN_STATUS_OK) return result;

    char handshake[1024];
    if (kw_string_equal(scheme, "wss") ||
        kw_string_equal(scheme, "auto")) {
        result = exchange(1, host, port, device_name, token, tls_skip,
                          frame, handshake, sizeof(handshake));
    }
    if ((result != KW_PLUGIN_STATUS_OK &&
         kw_string_equal(scheme, "auto")) ||
        kw_string_equal(scheme, "ws")) {
        result = exchange(0, host, port, device_name, token, tls_skip,
                          frame, handshake, sizeof(handshake));
    }
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (text_contains(handshake, "ms.channel.unauthorized")) {
        return KW_PLUGIN_STATUS_AUTH_FAILED;
    }
    if (!text_contains(handshake, "ms.channel.connect")) {
        return KW_PLUGIN_STATUS_DEVICE_ERROR;
    }
    char returned_token[128];
    if (extract_token(handshake, returned_token, sizeof(returned_token)) &&
        !kw_string_equal(returned_token, token)) {
        result = s_host->setting_set_string(
            PLUGIN_ID, "token", returned_token);
        if (result != KW_PLUGIN_STATUS_OK) return result;
    }
    return KW_PLUGIN_STATUS_OK;
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
    .display_name = "Samsung TV Network",
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
