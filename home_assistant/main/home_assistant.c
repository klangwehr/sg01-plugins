// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "home_assistant"

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "Home Assistant hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 95,
    },
    {
        .key = "port",
        .label = "API port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 8123,
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
        .key = "token",
        .label = "Long-lived access token",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED | KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 1024,
    },
    {
        .key = "timeout_ms",
        .label = "Request timeout (ms)",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 5000,
        .minimum_u32 = 500,
        .maximum_u32 = 30000,
    },
};

static kw_plugin_status_t load_config(char host[96],
                                      uint32_t *port,
                                      uint32_t *use_tls,
                                      char token[1025],
                                      uint32_t *timeout)
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 96, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_u32(
        s_host, PLUGIN_ID, "port", 8123, 1, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_u32(
        s_host, PLUGIN_ID, "use_tls", 0, 0, 1, use_tls);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result = kw_setting_string(
        s_host, PLUGIN_ID, "token", token, 1025, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return kw_setting_u32(
        s_host, PLUGIN_ID, "timeout_ms", 5000, 500, 30000, timeout);
}

static uint8_t split_call(const char *action,
                          char domain[33],
                          char service[33],
                          char entity[129])
{
    if (!kw_string_starts_with(action, "call:")) return 0;
    const char *cursor = action + 5;
    size_t domain_length = 0;
    while (cursor[domain_length] && cursor[domain_length] != '.') {
        domain_length++;
    }
    if (domain_length == 0 || domain_length >= 33 ||
        cursor[domain_length] != '.') {
        return 0;
    }
    size_t service_start = domain_length + 1;
    size_t service_length = 0;
    while (cursor[service_start + service_length] &&
           cursor[service_start + service_length] != ':') {
        service_length++;
    }
    if (service_length == 0 || service_length >= 33 ||
        cursor[service_start + service_length] != ':') {
        return 0;
    }
    const char *entity_start =
        cursor + service_start + service_length + 1;
    if (!entity_start[0] || kw_string_length(entity_start) >= 129) return 0;
    for (size_t i = 0; i < domain_length; i++) domain[i] = cursor[i];
    domain[domain_length] = '\0';
    for (size_t i = 0; i < service_length; i++) {
        service[i] = cursor[service_start + i];
    }
    service[service_length] = '\0';
    kw_copy(entity, 129, entity_start);
    return kw_identifier_valid(domain, 0) &&
           kw_identifier_valid(service, 0) &&
           kw_identifier_valid(entity, 1);
}

static uint8_t action_request(const char *action,
                              char path[160],
                              char body[160])
{
    char domain[33];
    char service[33];
    char entity[129];
    if (kw_string_starts_with(action, "scene:")) {
        kw_copy(domain, sizeof(domain), "scene");
        kw_copy(service, sizeof(service), "turn_on");
        kw_copy(entity, sizeof(entity), action + 6);
    } else if (kw_string_starts_with(action, "script:")) {
        kw_copy(domain, sizeof(domain), "script");
        kw_copy(service, sizeof(service), "turn_on");
        kw_copy(entity, sizeof(entity), action + 7);
    } else if (!split_call(action, domain, service, entity)) {
        return 0;
    }
    if (!kw_identifier_valid(domain, 0) ||
        !kw_identifier_valid(service, 0) ||
        !kw_identifier_valid(entity, 1)) {
        return 0;
    }
    size_t offset = 0;
    path[0] = '\0';
    if (!kw_append(path, 160, &offset, "/api/services/") ||
        !kw_append(path, 160, &offset, domain) ||
        !kw_append_char(path, 160, &offset, '/') ||
        !kw_append(path, 160, &offset, service)) {
        return 0;
    }
    offset = 0;
    body[0] = '\0';
    return kw_append(body, 160, &offset, "{\"entity_id\":\"") &&
           kw_append(body, 160, &offset, entity) &&
           kw_append(body, 160, &offset, "\"}");
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
    char host[96], token[1025];
    uint32_t port, use_tls, timeout;
    return load_config(host, &port, &use_tls, token, &timeout);
}

static kw_plugin_status_t execute_action(const char *action)
{
    char host[96], token[1025], authorization[1040];
    char path[160], body[160];
    uint32_t port, use_tls, timeout;
    kw_plugin_status_t result =
        load_config(host, &port, &use_tls, token, &timeout);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!action_request(action, path, body)) {
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
    };
    kw_plugin_http_request_v1_t request;
    kw_http_request_init(&request);
    request.method = KW_PLUGIN_HTTP_POST;
    request.use_tls = use_tls != 0;
    request.host = host;
    request.port = (uint16_t)port;
    request.path = path;
    request.content_type = "application/json";
    request.body = body;
    request.body_length = kw_string_length(body);
    request.timeout_ms = timeout;
    request.headers = headers;
    request.header_count = 1;
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
    .display_name = "Home Assistant",
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
    .settings = s_settings,
    .setting_count = sizeof(s_settings) / sizeof(s_settings[0]),
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
