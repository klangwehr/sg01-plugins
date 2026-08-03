// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "lg_webos"

typedef struct {
    const char *action;
    const char *button;
} button_map_t;

typedef struct {
    const char *action;
    const char *uri;
    const char *payload;
} service_map_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const button_map_t s_buttons[] = {
    {"home", "HOME"}, {"back", "BACK"}, {"up", "UP"},
    {"down", "DOWN"}, {"left", "LEFT"}, {"right", "RIGHT"},
    {"select", "ENTER"}, {"ok", "ENTER"}, {"enter", "ENTER"},
    {"play_pause", "PLAYPAUSE"}, {"play", "PLAY"}, {"pause", "PAUSE"},
    {"stop", "STOP"},
};

static const service_map_t s_services[] = {
    {"status_query",
     "ssap://com.webos.applicationManager/getForegroundAppInfo", "{}"},
    {"volume_up", "ssap://audio/volumeUp", "{}"},
    {"volume_down", "ssap://audio/volumeDown", "{}"},
    {"mute", "ssap://audio/setMute", "{\"mute\":true}"},
    {"mute_on", "ssap://audio/setMute", "{\"mute\":true}"},
    {"mute_off", "ssap://audio/setMute", "{\"mute\":false}"},
    {"mute_status", "ssap://audio/getMute", "{}"},
    {"volume_status", "ssap://audio/getVolume", "{}"},
    {"power_off", "ssap://system/turnOff", "{}"},
    {"standby", "ssap://system/turnOff", "{}"},
};

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"pair", "Pair / approve on TV", "system"},
    {"home", "Home", "navigation"},
    {"back", "Back", "navigation"},
    {"up", "Up", "navigation"},
    {"down", "Down", "navigation"},
    {"left", "Left", "navigation"},
    {"right", "Right", "navigation"},
    {"select", "Select", "navigation"},
    {"ok", "OK", "navigation"},
    {"enter", "Enter", "navigation"},
    {"play_pause", "Play/Pause", "playback"},
    {"play", "Play", "playback"},
    {"pause", "Pause", "playback"},
    {"stop", "Stop", "playback"},
    {"volume_up", "Volume Up", "volume"},
    {"volume_down", "Volume Down", "volume"},
    {"mute", "Mute", "volume"},
    {"mute_on", "Mute On", "volume"},
    {"mute_off", "Mute Off", "volume"},
    {"mute_status", "Mute Status", "volume"},
    {"volume_status", "Volume Status", "volume"},
    {"power_off", "Power Off", "power"},
    {"standby", "Standby", "power"},
    {"status_query", "Status Query", "system"},
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
        .label = "WebSocket port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 3000,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
    {
        .key = "client_key",
        .label = "Pairing client key",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 127,
    },
};

static uint8_t extract_json_string(const char *json,
                                   const char *name,
                                   char *output,
                                   size_t output_size)
{
    char marker[40];
    size_t marker_offset = 0;
    marker[0] = '\0';
    if (!kw_append_char(marker, sizeof(marker), &marker_offset, '"') ||
        !kw_append(marker, sizeof(marker), &marker_offset, name) ||
        !kw_append(marker, sizeof(marker), &marker_offset, "\":\"")) {
        return 0;
    }
    for (size_t i = 0; json && json[i]; i++) {
        size_t j = 0;
        while (marker[j] && json[i + j] == marker[j]) j++;
        if (marker[j]) continue;
        size_t start = i + j;
        size_t length = 0;
        while (json[start + length] &&
               json[start + length] != '"' &&
               length + 1u < output_size) {
            char c = json[start + length];
            if (c < 0x20 || c == '\\') return 0;
            output[length++] = c;
        }
        if (!length || json[start + length] != '"') return 0;
        output[length] = '\0';
        return 1;
    }
    return 0;
}

static uint8_t contains(const char *value, const char *needle)
{
    if (!value || !needle || !needle[0]) return 0;
    for (size_t i = 0; value[i]; i++) {
        size_t j = 0;
        while (needle[j] && value[i + j] == needle[j]) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}

static kw_plugin_status_t target(char host[64],
                                 uint32_t *port,
                                 char client_key[128])
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result =
        kw_setting_u32(s_host, PLUGIN_ID, "port", 3000, 1, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return kw_setting_string(
        s_host, PLUGIN_ID, "client_key", client_key, 128, 0);
}

static uint8_t build_url(char *url,
                         size_t url_size,
                         const char *host,
                         uint32_t port)
{
    size_t offset = 0;
    url[0] = '\0';
    return kw_append(url, url_size, &offset, "ws://") &&
           kw_append(url, url_size, &offset, host) &&
           kw_append_char(url, url_size, &offset, ':') &&
           kw_append_u32(url, url_size, &offset, port) &&
           kw_append_char(url, url_size, &offset, '/');
}

static uint8_t build_register_frame(char *frame,
                                    size_t frame_size,
                                    const char *client_key,
                                    uint8_t force_pairing)
{
    size_t offset = 0;
    frame[0] = '\0';
    if (!kw_append(frame, frame_size, &offset,
                   "{\"type\":\"register\",\"id\":\"register_0\","
                   "\"payload\":{")) {
        return 0;
    }
    if (force_pairing) {
        if (!kw_append(frame, frame_size, &offset,
                       "\"pairingType\":\"PROMPT\",")) {
            return 0;
        }
    } else if (client_key && client_key[0]) {
        if (!kw_append(frame, frame_size, &offset, "\"client-key\":\"") ||
            !kw_append(frame, frame_size, &offset, client_key) ||
            !kw_append(frame, frame_size, &offset, "\",")) {
            return 0;
        }
    }
    return kw_append(
        frame, frame_size, &offset,
        "\"manifest\":{\"manifestVersion\":1,\"permissions\":["
        "\"LAUNCH\",\"CONTROL_AUDIO\",\"CONTROL_INPUT_TEXT\","
        "\"CONTROL_MOUSE_AND_KEYBOARD\",\"READ_INSTALLED_APPS\"]}}}");
}

static uint8_t build_request_frame(char *frame,
                                   size_t frame_size,
                                   const char *uri,
                                   const char *payload)
{
    size_t offset = 0;
    frame[0] = '\0';
    return kw_append(frame, frame_size, &offset,
                     "{\"type\":\"request\",\"id\":\"cmd_1\",\"uri\":\"") &&
           kw_append(frame, frame_size, &offset, uri) &&
           kw_append(frame, frame_size, &offset, "\",\"payload\":") &&
           kw_append(frame, frame_size, &offset, payload) &&
           kw_append_char(frame, frame_size, &offset, '}');
}

static kw_plugin_status_t service_exchange(
    const char *url,
    const char *register_frame,
    const char *command_frame,
    char *register_response,
    size_t register_response_size,
    char *command_response,
    size_t command_response_size)
{
    size_t register_length = 0;
    size_t command_length = 0;
    kw_plugin_websocket_frame_v1_t frames[2] = {
        {
            .text = register_frame,
            .text_length = kw_string_length(register_frame),
            .response = register_response,
            .response_size = register_response_size,
            .response_length = &register_length,
        },
        {
            .text = command_frame,
            .text_length = command_frame ? kw_string_length(command_frame) : 0,
            .response = command_response,
            .response_size = command_response_size,
            .response_length = &command_length,
        },
    };
    kw_plugin_websocket_request_v1_t request = {0};
    request.struct_size = sizeof(request);
    request.uri = url;
    request.timeout_ms = 10000;
    request.frames = frames;
    request.frame_count = command_frame ? 2 : 1;
    return s_host->websocket_exchange(&request);
}

static kw_plugin_status_t save_returned_key(const char *response)
{
    char returned_key[128];
    if (extract_json_string(
            response, "client-key", returned_key, sizeof(returned_key))) {
        return s_host->setting_set_string(
            PLUGIN_ID, "client_key", returned_key);
    }
    return contains(response, "\"type\":\"registered\"")
        ? KW_PLUGIN_STATUS_OK : KW_PLUGIN_STATUS_AUTH_FAILED;
}

static const char *button_for_action(const char *action)
{
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        if (kw_string_equal(action, s_buttons[i].action)) {
            return s_buttons[i].button;
        }
    }
    return 0;
}

static const service_map_t *service_for_action(const char *action)
{
    for (size_t i = 0; i < sizeof(s_services) / sizeof(s_services[0]); i++) {
        if (kw_string_equal(action, s_services[i].action)) {
            return &s_services[i];
        }
    }
    return 0;
}

static kw_plugin_status_t pair(const char *url)
{
    char register_frame[768];
    char register_response[1024];
    if (!build_register_frame(
            register_frame, sizeof(register_frame), "", 1)) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    kw_plugin_status_t result = service_exchange(
        url, register_frame, 0, register_response,
        sizeof(register_response), 0, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return save_returned_key(register_response);
}

static kw_plugin_status_t service_action(const char *url,
                                         const char *client_key,
                                         const char *uri,
                                         const char *payload,
                                         char *response,
                                         size_t response_size)
{
    char register_frame[768];
    char command_frame[512];
    char register_response[1024];
    if (!client_key[0]) return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    if (!build_register_frame(
            register_frame, sizeof(register_frame), client_key, 0) ||
        !build_request_frame(
            command_frame, sizeof(command_frame), uri, payload)) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    kw_plugin_status_t result = service_exchange(
        url, register_frame, command_frame, register_response,
        sizeof(register_response), response, response_size);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = save_returned_key(register_response);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return contains(response, "\"type\":\"error\"")
        ? KW_PLUGIN_STATUS_DEVICE_ERROR : KW_PLUGIN_STATUS_OK;
}

static kw_plugin_status_t button_action(const char *url,
                                        const char *client_key,
                                        const char *button)
{
    char pointer_response[1024];
    kw_plugin_status_t result = service_action(
        url, client_key,
        "ssap://com.webos.service.networkinput/getPointerInputSocket",
        "{}", pointer_response, sizeof(pointer_response));
    if (result != KW_PLUGIN_STATUS_OK) return result;
    char pointer_url[256];
    if (!extract_json_string(
            pointer_response, "socketPath",
            pointer_url, sizeof(pointer_url))) {
        return KW_PLUGIN_STATUS_DEVICE_ERROR;
    }

    char frame[96];
    size_t offset = 0;
    frame[0] = '\0';
    if (!kw_append(frame, sizeof(frame), &offset, "type:button\nname:") ||
        !kw_append(frame, sizeof(frame), &offset, button) ||
        !kw_append(frame, sizeof(frame), &offset, "\n\n")) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    kw_plugin_websocket_frame_v1_t ws_frame = {
        .text = frame,
        .text_length = offset,
    };
    kw_plugin_websocket_request_v1_t request = {0};
    request.struct_size = sizeof(request);
    request.uri = pointer_url;
    request.timeout_ms = 5000;
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
    char host[64], client_key[128];
    uint32_t port;
    return target(host, &port, client_key);
}

static kw_plugin_status_t execute_action(const char *action)
{
    char host[64], client_key[128], url[128];
    uint32_t port;
    kw_plugin_status_t result = target(host, &port, client_key);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!build_url(url, sizeof(url), host, port)) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    if (kw_string_equal(action, "pair")) return pair(url);

    const char *button = button_for_action(action);
    if (button) return button_action(url, client_key, button);
    const service_map_t *service = service_for_action(action);
    char payload[48];
    if (!service && kw_string_starts_with(action, "volume:")) {
        uint32_t volume = 0;
        if (!kw_parse_u32(action + 7, 0, 100, &volume)) {
            return KW_PLUGIN_STATUS_UNSUPPORTED;
        }
        size_t offset = 0;
        payload[0] = '\0';
        if (!kw_append(payload, sizeof(payload), &offset, "{\"volume\":") ||
            !kw_append_u32(payload, sizeof(payload), &offset, volume) ||
            !kw_append_char(payload, sizeof(payload), &offset, '}')) {
            return KW_PLUGIN_STATUS_INTERNAL_ERROR;
        }
        char response[1024];
        return service_action(
            url, client_key, "ssap://audio/setVolume", payload,
            response, sizeof(response));
    }
    if (!service) return KW_PLUGIN_STATUS_UNSUPPORTED;
    char response[1024];
    return service_action(
        url, client_key, service->uri, service->payload,
        response, sizeof(response));
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
    .display_name = "LG webOS",
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
