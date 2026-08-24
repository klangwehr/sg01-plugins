// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "shelly"

typedef enum {
    KIND_RELAY,
    KIND_SWITCH,
    KIND_LIGHT,
    KIND_COVER,
} device_kind_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"on", "On", "power"},
    {"off", "Off", "power"},
    {"toggle", "Toggle", "power"},
    {"open", "Open", "cover"},
    {"close", "Close", "cover"},
    {"stop", "Stop", "cover"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "Shelly IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 95,
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
        .key = "generation",
        .label = "Generation (auto, gen1, or gen2)",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "auto",
        .maximum_length = 4,
    },
    {
        .key = "channel",
        .label = "Channel",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 0,
        .minimum_u32 = 0,
        .maximum_u32 = 15,
    },
    {
        .key = "kind",
        .label = "Device kind (relay, switch, light, or cover)",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "relay",
        .maximum_length = 6,
    },
    {
        .key = "username",
        .label = "Gen1 username",
        .type = KW_PLUGIN_SETTING_STRING,
        .default_string = "",
        .maximum_length = 95,
    },
    {
        .key = "password",
        .label = "Gen1 password",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 95,
    },
};

static kw_plugin_status_t load_config(char host[96],
                                      uint32_t *port,
                                      char generation[5],
                                      uint32_t *channel,
                                      char kind[7],
                                      char username[96],
                                      char password[96])
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 96, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_u32(
        s_host, PLUGIN_ID, "port", 80, 1, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "generation", generation, 5, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!generation[0]) kw_copy(generation, 5, "auto");
    if (!kw_string_equal(generation, "auto") &&
        !kw_string_equal(generation, "gen1") &&
        !kw_string_equal(generation, "gen2")) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
    result = kw_setting_u32(
        s_host, PLUGIN_ID, "channel", 0, 0, 15, channel);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "kind", kind, 7, 0);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!kind[0]) kw_copy(kind, 7, "relay");
    if (!kw_string_equal(kind, "relay") &&
        !kw_string_equal(kind, "switch") &&
        !kw_string_equal(kind, "light") &&
        !kw_string_equal(kind, "cover")) {
        return KW_PLUGIN_STATUS_NOT_CONFIGURED;
    }
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

static kw_plugin_status_t request(const char *host,
                                  uint32_t port,
                                  const char *authorization,
                                  kw_plugin_http_method_t method,
                                  const char *path,
                                  const char *body,
                                  char *response,
                                  size_t response_size)
{
    kw_plugin_http_header_v1_t header = {
        .name = "Authorization",
        .value = authorization,
    };
    size_t response_length = 0;
    kw_plugin_http_request_v1_t http;
    kw_http_request_init(&http);
    http.method = method;
    http.host = host;
    http.port = (uint16_t)port;
    http.path = path;
    http.content_type = body ? "application/json" : 0;
    http.body = body;
    http.body_length = kw_string_length(body);
    http.response = response;
    http.response_size = response_size;
    http.response_length = response ? &response_length : 0;
    http.headers = authorization && authorization[0] ? &header : 0;
    http.header_count = authorization && authorization[0] ? 1 : 0;
    return s_host->http_request(&http);
}

static int32_t effective_generation(const char *host,
                                    uint32_t port,
                                    const char *configured,
                                    const char *authorization)
{
    if (kw_string_equal(configured, "gen1")) return 1;
    if (kw_string_equal(configured, "gen2")) return 2;
    char response[384];
    kw_plugin_status_t result = request(
        host, port, authorization, KW_PLUGIN_HTTP_GET,
        "/shelly", 0, response, sizeof(response));
    if (result != KW_PLUGIN_STATUS_OK) return -1;
    return contains(response, "\"gen\":2") ||
           contains(response, "\"gen\": 2") ? 2 : 1;
}

static device_kind_t parse_kind(const char *kind)
{
    if (kw_string_equal(kind, "switch")) return KIND_SWITCH;
    if (kw_string_equal(kind, "light")) return KIND_LIGHT;
    if (kw_string_equal(kind, "cover")) return KIND_COVER;
    return KIND_RELAY;
}

static uint8_t parse_action(const char *action,
                            device_kind_t configured_kind,
                            device_kind_t *kind,
                            const char **command,
                            int32_t *brightness)
{
    *kind = configured_kind;
    *command = action;
    *brightness = -1;
    const char *colon = action;
    while (*colon && *colon != ':') colon++;
    if (*colon == ':') {
        size_t prefix_length = (size_t)(colon - action);
        if (prefix_length == 5 && kw_string_starts_with(action, "relay")) {
            *kind = KIND_RELAY;
            *command = colon + 1;
        } else if (prefix_length == 6 &&
                   kw_string_starts_with(action, "switch")) {
            *kind = KIND_SWITCH;
            *command = colon + 1;
        } else if (prefix_length == 5 &&
                   kw_string_starts_with(action, "light")) {
            *kind = KIND_LIGHT;
            *command = colon + 1;
        } else if (prefix_length == 5 &&
                   kw_string_starts_with(action, "cover")) {
            *kind = KIND_COVER;
            *command = colon + 1;
        }
    }
    if (kw_string_starts_with(*command, "brightness:")) {
        if (*kind != KIND_LIGHT) return 0;
        const char *number = *command + 11;
        uint32_t value = 0;
        if (!*number) return 0;
        while (*number) {
            if (*number < '0' || *number > '9') return 0;
            value = value * 10u + (uint32_t)(*number++ - '0');
            if (value > 100u) return 0;
        }
        *brightness = (int32_t)value;
        *command = "brightness";
        return 1;
    }
    return kw_string_equal(*command, "on") ||
           kw_string_equal(*command, "off") ||
           kw_string_equal(*command, "toggle") ||
           kw_string_equal(*command, "open") ||
           kw_string_equal(*command, "close") ||
           kw_string_equal(*command, "stop");
}

static uint8_t gen1_request(device_kind_t kind,
                            const char *command,
                            int32_t brightness,
                            uint32_t channel,
                            char path[128])
{
    size_t offset = 0;
    path[0] = '\0';
    if (kind == KIND_RELAY || kind == KIND_SWITCH) {
        if (!kw_string_equal(command, "on") &&
            !kw_string_equal(command, "off") &&
            !kw_string_equal(command, "toggle")) return 0;
        return kw_append(path, 128, &offset, "/relay/") &&
               kw_append_u32(path, 128, &offset, channel) &&
               kw_append(path, 128, &offset, "?turn=") &&
               kw_append(path, 128, &offset, command);
    }
    if (kind == KIND_LIGHT) {
        if (!kw_append(path, 128, &offset, "/light/") ||
            !kw_append_u32(path, 128, &offset, channel)) return 0;
        if (brightness >= 0) {
            return kw_append(path, 128, &offset,
                             "?turn=on&brightness=") &&
                   kw_append_u32(path, 128, &offset,
                                 (uint32_t)brightness);
        }
        return (kw_string_equal(command, "on") ||
                kw_string_equal(command, "off") ||
                kw_string_equal(command, "toggle")) &&
               kw_append(path, 128, &offset, "?turn=") &&
               kw_append(path, 128, &offset, command);
    }
    if (kind == KIND_COVER) {
        return (kw_string_equal(command, "open") ||
                kw_string_equal(command, "close") ||
                kw_string_equal(command, "stop")) &&
               kw_append(path, 128, &offset, "/roller/") &&
               kw_append_u32(path, 128, &offset, channel) &&
               kw_append(path, 128, &offset, "?go=") &&
               kw_append(path, 128, &offset, command);
    }
    return 0;
}

static uint8_t gen2_request(device_kind_t kind,
                            const char *command,
                            int32_t brightness,
                            uint32_t channel,
                            char body[256])
{
    const char *component =
        kind == KIND_LIGHT ? "Light" :
        kind == KIND_COVER ? "Cover" : "Switch";
    const char *operation = 0;
    if (kw_string_equal(command, "toggle")) operation = "Toggle";
    else if (kw_string_equal(command, "open")) operation = "Open";
    else if (kw_string_equal(command, "close")) operation = "Close";
    else if (kw_string_equal(command, "stop")) operation = "Stop";
    else if (kw_string_equal(command, "on") ||
             kw_string_equal(command, "off") ||
             brightness >= 0) operation = "Set";
    if (!operation) return 0;
    if (kind == KIND_COVER &&
        (kw_string_equal(command, "on") ||
         kw_string_equal(command, "off") ||
         kw_string_equal(command, "toggle") ||
         brightness >= 0)) return 0;

    size_t offset = 0;
    body[0] = '\0';
    if (!kw_append(body, 256, &offset,
                   "{\"id\":1,\"src\":\"signalgeraet\",\"method\":\"") ||
        !kw_append(body, 256, &offset, component) ||
        !kw_append_char(body, 256, &offset, '.') ||
        !kw_append(body, 256, &offset, operation) ||
        !kw_append(body, 256, &offset, "\",\"params\":{\"id\":") ||
        !kw_append_u32(body, 256, &offset, channel)) return 0;
    if (brightness >= 0) {
        if (!kw_append(body, 256, &offset,
                       ",\"on\":true,\"brightness\":") ||
            !kw_append_u32(body, 256, &offset,
                           (uint32_t)brightness)) return 0;
    } else if (kw_string_equal(command, "on") ||
               kw_string_equal(command, "off")) {
        if (!kw_append(body, 256, &offset, ",\"on\":") ||
            !kw_append(body, 256, &offset,
                       kw_string_equal(command, "on") ? "true" : "false")) {
            return 0;
        }
    }
    return kw_append(body, 256, &offset, "}}");
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
    char host[96], generation[5], kind[7], username[96], password[96];
    uint32_t port, channel;
    return load_config(host, &port, generation, &channel, kind,
                       username, password);
}

static kw_plugin_status_t execute_action(const char *action)
{
    char host[96], generation[5], kind_text[7];
    char username[96], password[96], authorization[288];
    uint32_t port, channel;
    kw_plugin_status_t result = load_config(
        host, &port, generation, &channel, kind_text, username, password);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    authorization[0] = '\0';
    if ((username[0] || password[0]) &&
        !kw_basic_authorization(
            username, password, authorization, sizeof(authorization))) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    int32_t generation_number = effective_generation(
        host, port, generation, authorization);
    if (generation_number < 0) return KW_PLUGIN_STATUS_UNREACHABLE;
    if (generation_number == 2 && authorization[0]) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }

    device_kind_t kind;
    const char *command;
    int32_t brightness;
    if (!parse_action(action, parse_kind(kind_text), &kind,
                      &command, &brightness)) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    if (generation_number == 1) {
        char path[128];
        if (!gen1_request(kind, command, brightness, channel, path)) {
            return KW_PLUGIN_STATUS_UNSUPPORTED;
        }
        return request(host, port, authorization, KW_PLUGIN_HTTP_GET,
                       path, 0, 0, 0);
    }
    char body[256];
    if (!gen2_request(kind, command, brightness, channel, body)) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }
    char response[256];
    result = request(host, port, 0, KW_PLUGIN_HTTP_POST,
                     "/rpc", body, response, sizeof(response));
    if (result == KW_PLUGIN_STATUS_OK &&
        contains(response, "\"error\"")) {
        return KW_PLUGIN_STATUS_DEVICE_ERROR;
    }
    return result;
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
    .display_name = "Shelly",
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
    .discovery_profile = "shelly",
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
