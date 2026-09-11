// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#include <string.h>

#define PLUGIN_ID "bluos"
#define RESPONSE_MAX 8192u
#define STATUS_MAX 6144u
#define STATUS_CACHE_MS 15000u

typedef struct { const char *name; const char *path; } bluos_action_t;
typedef struct {
    char *data;
    size_t size;
    size_t length;
    uint8_t first;
    uint8_t valid;
} json_writer_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;
static char s_status_cache[STATUS_MAX];
static uint64_t s_status_cache_time;

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text && text[length]) length++;
    return length;
}

static uint8_t starts_with(const char *text, const char *prefix)
{
    size_t i = 0;
    if (!text || !prefix) return 0;
    while (prefix[i] && text[i] == prefix[i]) i++;
    return prefix[i] == '\0';
}

static const char *find_text(const char *text, const char *needle)
{
    if (!text || !needle || !needle[0]) return text;
    for (const char *candidate = text; *candidate; candidate++)
        if (starts_with(candidate, needle)) return candidate;
    return NULL;
}

static const char *find_char(const char *text, char wanted)
{
    while (text && *text) {
        if (*text == wanted) return text;
        text++;
    }
    return NULL;
}

static void decode_xml_entities(char *value)
{
    char *read = value;
    char *write = value;
    while (read && *read) {
        struct { const char *entity; char decoded; } entities[] = {
            {"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''},
            {"&lt;", '<'}, {"&gt;", '>'},
        };
        uint8_t matched = 0;
        for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
            if (starts_with(read, entities[i].entity)) {
                *write++ = entities[i].decoded;
                read += text_length(entities[i].entity);
                matched = 1;
                break;
            }
        }
        if (!matched) *write++ = *read++;
    }
    if (write) *write = '\0';
}

/* Compatibility commands remain stable for existing activities. Sources are
 * resolved from the player's API instead of assuming preset numbers. */
static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"play", "Play", "playback"},
    {"pause", "Pause", "playback"},
    {"skip", "Skip", "playback"},
    {"back", "Back", "playback"},
    {"optical", "Digital / optical input", "source"},
    {"bluetooth", "Bluetooth", "source"},
    {"spotify", "Spotify", "source"},
    {"vol_up", "Volume Up", "volume"},
    {"vol_down", "Volume Down", "volume"},
};

static const bluos_action_t s_requests[] = {
    {"play", "/Play"}, {"pause", "/Pause"},
    {"skip", "/Skip"}, {"back", "/Back"},
    {"vol_up", "/Volume?level=+2"},
    {"vol_down", "/Volume?level=-2"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {.key = "host", .label = "BluOS IP address or hostname",
     .type = KW_PLUGIN_SETTING_STRING, .flags = KW_PLUGIN_SETTING_REQUIRED,
     .default_string = "", .maximum_length = 63},
    {.key = "port", .label = "HTTP port", .type = KW_PLUGIN_SETTING_U32,
     .default_u32 = 11000, .minimum_u32 = 1, .maximum_u32 = 65535},
};

static kw_plugin_status_t target(char host[64], uint32_t *port)
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return kw_setting_u32(s_host, PLUGIN_ID, "port", 11000, 1, 65535, port);
}

static kw_plugin_status_t request_path(const char *path, char *response,
                                       size_t response_size,
                                       size_t *response_length)
{
    char host[64];
    uint32_t port;
    kw_plugin_status_t result = target(host, &port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    kw_plugin_http_request_v1_t request;
    kw_http_request_init(&request);
    request.method = KW_PLUGIN_HTTP_GET;
    request.host = host;
    request.port = (uint16_t)port;
    request.path = path;
    request.response = response;
    request.response_size = response_size;
    request.response_length = response_length;
    result = s_host->http_request(&request);
    if (result == KW_PLUGIN_STATUS_OK && response && response_size) {
        size_t length = response_length ? *response_length : 0;
        response[length < response_size ? length : response_size - 1] = '\0';
    }
    return result;
}

static const char *attribute(const char *tag, const char *end,
                             const char *name, char *value, size_t value_size)
{
    if (!tag || !end || !name || !value || value_size == 0) return NULL;
    size_t name_length = text_length(name);
    for (const char *p = tag; p + name_length + 2 < end; p++) {
        if ((p == tag || p[-1] == '<' || p[-1] == ' ' || p[-1] == '\t') &&
            starts_with(p, name) && p[name_length] == '=') {
            char quote = p[name_length + 1];
            if (quote != '\'' && quote != '"') continue;
            const char *start = p + name_length + 2;
            const char *finish = start;
            while (finish < end && *finish != quote) finish++;
            if (finish >= end) return NULL;
            size_t length = (size_t)(finish - start);
            if (length >= value_size) length = value_size - 1;
            memcpy(value, start, length);
            value[length] = '\0';
            decode_xml_entities(value);
            return finish + 1;
        }
    }
    return NULL;
}

static const char *next_element(const char *xml, const char *element,
                                const char **end)
{
    char marker[24];
    size_t element_length = text_length(element);
    if (!element_length || element_length + 2 > sizeof(marker)) return NULL;
    marker[0] = '<';
    memcpy(marker + 1, element, element_length);
    marker[element_length + 1] = '\0';
    const char *start = xml;
    do {
        start = find_text(start, marker);
        if (!start) return NULL;
        char delimiter = start[element_length + 1];
        if (delimiter == '>' || delimiter == '/' || delimiter == ' ' ||
            delimiter == '\t' || delimiter == '\r' || delimiter == '\n')
            break;
        start++;
    } while (*start);
    const char *finish = find_char(start, '>');
    if (!finish) return NULL;
    if (end) *end = finish;
    return start;
}

static uint8_t append_text(json_writer_t *writer, const char *value)
{
    if (!writer || !writer->valid || writer->length >= writer->size) return 0;
    size_t length = text_length(value);
    if (length >= writer->size - writer->length) {
        writer->valid = 0;
        return 0;
    }
    memcpy(writer->data + writer->length, value, length);
    writer->length += length;
    writer->data[writer->length] = '\0';
    return 1;
}

static uint8_t append_char(json_writer_t *writer, char value)
{
    if (!writer || !writer->valid || writer->length + 1 >= writer->size) {
        if (writer) writer->valid = 0;
        return 0;
    }
    writer->data[writer->length++] = value;
    writer->data[writer->length] = '\0';
    return 1;
}

static uint8_t append_json_string(json_writer_t *writer, const char *value)
{
    if (!append_char(writer, '"')) return 0;
    for (const unsigned char *p = (const unsigned char *)(value ? value : "");
         *p; p++) {
        if (*p == '"' || *p == '\\') {
            if (!append_char(writer, '\\') || !append_char(writer, (char)*p)) return 0;
        } else if (*p == '\n') {
            if (!append_text(writer, "\\n")) return 0;
        } else if (*p == '\r') {
            if (!append_text(writer, "\\r")) return 0;
        } else if (*p == '\t') {
            if (!append_text(writer, "\\t")) return 0;
        } else if (*p >= 0x20 && !append_char(writer, (char)*p)) {
            return 0;
        }
    }
    return append_char(writer, '"');
}

static uint8_t append_action(json_writer_t *writer, const char *name,
                             const char *label, const char *category)
{
    if (!writer->first && !append_char(writer, ',')) return 0;
    writer->first = 0;
    if (!append_text(writer, "{\"name\":")) return 0;
    if (!append_json_string(writer, name)) return 0;
    if (!append_text(writer, ",\"label\":")) return 0;
    if (!append_json_string(writer, label)) return 0;
    if (!append_text(writer, ",\"category\":")) return 0;
    if (!append_json_string(writer, category)) return 0;
    return append_char(writer, '}');
}

static uint8_t action_token(const char *prefix, const char *id,
                            char *output, size_t output_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    for (const char *p = prefix; *p; p++) {
        if (used + 1 >= output_size) return 0;
        output[used++] = *p;
    }
    for (const unsigned char *p = (const unsigned char *)id; *p; p++) {
        uint8_t safe = (*p >= 'a' && *p <= 'z') ||
                       (*p >= 'A' && *p <= 'Z') ||
                       (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
                       *p == '.';
        if (safe) {
            if (used + 1 >= output_size) return 0;
            output[used++] = (char)*p;
        } else {
            if (used + 3 >= output_size) return 0;
            output[used++] = '%';
            output[used++] = hex[*p >> 4];
            output[used++] = hex[*p & 0x0f];
        }
    }
    output[used] = '\0';
    return used > text_length(prefix);
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint8_t decode_token(const char *value, char *output, size_t output_size)
{
    size_t used = 0;
    for (size_t i = 0; value && value[i]; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c == '%') {
            int high = hex_value(value[i + 1]);
            int low = high >= 0 ? hex_value(value[i + 2]) : -1;
            if (low < 0) return 0;
            c = (unsigned char)((high << 4) | low);
            i += 2;
        }
        if (c < 0x20 || used + 1 >= output_size) return 0;
        output[used++] = (char)c;
    }
    output[used] = '\0';
    return used > 0;
}

static uint8_t compatible_source(const char *action, const char *id,
                                 const char *input_type)
{
    if (kw_string_equal(action, "spotify"))
        return kw_string_equal(id, "Spotify");
    if (kw_string_equal(action, "bluetooth"))
        return kw_string_equal(input_type, "bluetooth");
    if (kw_string_equal(action, "optical"))
        return kw_string_equal(input_type, "spdif") ||
               find_text(input_type, "optical") != NULL;
    return 0;
}

static kw_plugin_status_t execute_source(const char *action)
{
    char wanted_id[128] = "";
    if (starts_with(action, "source:") &&
        !decode_token(action + 7, wanted_id, sizeof(wanted_id)))
        return KW_PLUGIN_STATUS_UNSUPPORTED;

    char *xml = s_host->alloc(RESPONSE_MAX);
    if (!xml) return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    size_t response_length = 0;
    kw_plugin_status_t result = request_path(
        "/RadioBrowse?service=Capture", xml, RESPONSE_MAX, &response_length);
    if (result != KW_PLUGIN_STATUS_OK) {
        s_host->free(xml);
        return result;
    }
    const char *cursor = xml;
    char id[128], type[64], url[512];
    while ((cursor = next_element(cursor, "item", NULL)) != NULL) {
        const char *end = find_char(cursor, '>');
        if (!end) break;
        id[0] = type[0] = url[0] = '\0';
        attribute(cursor, end, "id", id, sizeof(id));
        attribute(cursor, end, "inputType", type, sizeof(type));
        attribute(cursor, end, "URL", url, sizeof(url));
        if (url[0] && ((wanted_id[0] && kw_string_equal(id, wanted_id)) ||
                      (!wanted_id[0] && compatible_source(action, id, type)))) {
            char path[560];
            size_t prefix_length = text_length("/Play?url=");
            size_t url_length = text_length(url);
            s_host->free(xml);
            if (prefix_length + url_length >= sizeof(path))
                return KW_PLUGIN_STATUS_UNSUPPORTED;
            memcpy(path, "/Play?url=", prefix_length);
            memcpy(path + prefix_length, url, url_length + 1);
            return request_path(path, NULL, 0, NULL);
        }
        cursor = end + 1;
    }
    s_host->free(xml);
    return KW_PLUGIN_STATUS_UNSUPPORTED;
}

static kw_plugin_status_t execute_preset(const char *action)
{
    char id[96];
    if (!decode_token(action + 7, id, sizeof(id)))
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    for (const unsigned char *p = (const unsigned char *)id; *p; p++)
        if (*p < '0' || *p > '9') return KW_PLUGIN_STATUS_UNSUPPORTED;
    char path[128];
    size_t prefix_length = text_length("/Preset?id=");
    size_t id_length = text_length(id);
    if (prefix_length + id_length >= sizeof(path))
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    memcpy(path, "/Preset?id=", prefix_length);
    memcpy(path + prefix_length, id, id_length + 1);
    return request_path(path, NULL, 0, NULL);
}

static kw_plugin_status_t bind_host(const kw_plugin_host_api_v1_t *host)
{
    if (!host || host->abi_major != KW_PLUGIN_ABI_MAJOR ||
        host->abi_minor < 2u ||
        host->struct_size < offsetof(kw_plugin_host_api_v1_t, tcp_exchange) ||
        !host->http_request || !host->alloc || !host->free ||
        !host->monotonic_ms || !host->setting_get_string ||
        !host->setting_get_u32)
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    s_host = host;
    return KW_PLUGIN_STATUS_OK;
}

static int32_t initialize(void) { return s_host ? 0 : -1; }
static int32_t start(void)
{
    s_started = 1;
    s_status_cache_time = 0;
    return 0;
}
static void stop(void) { s_started = 0; }
static void deinitialize(void) { s_started = 0; }
static kw_plugin_status_t validate_config(void)
{
    char host[64]; uint32_t port;
    return target(host, &port);
}

static kw_plugin_status_t execute_action(const char *action)
{
    if (!action) return KW_PLUGIN_STATUS_UNSUPPORTED;
    if (starts_with(action, "source:") ||
        kw_string_equal(action, "optical") ||
        kw_string_equal(action, "bluetooth") ||
        kw_string_equal(action, "spotify"))
        return execute_source(action);
    if (starts_with(action, "preset:")) return execute_preset(action);
    for (size_t i = 0; i < sizeof(s_requests) / sizeof(s_requests[0]); i++)
        if (kw_string_equal(action, s_requests[i].name))
            return request_path(s_requests[i].path, NULL, 0, NULL);
    return KW_PLUGIN_STATUS_UNSUPPORTED;
}

static void append_player(json_writer_t *writer, const char *xml)
{
    const char *end = NULL;
    const char *sync = next_element(xml, "SyncStatus", &end);
    char name[96] = "", model[96] = "", version[32] = "";
    if (sync) {
        attribute(sync, end, "name", name, sizeof(name));
        attribute(sync, end, "modelName", model, sizeof(model));
        attribute(sync, end, "version", version, sizeof(version));
    }
    append_text(writer, ",\"player\":{\"name\":");
    append_json_string(writer, name);
    append_text(writer, ",\"model\":"); append_json_string(writer, model);
    append_text(writer, ",\"version\":"); append_json_string(writer, version);
    append_char(writer, '}');
}

static void append_sources(json_writer_t *writer, const char *xml)
{
    const char *cursor = xml;
    char id[128], label[160], action[400];
    while ((cursor = next_element(cursor, "item", NULL)) != NULL) {
        const char *end = find_char(cursor, '>');
        if (!end) return;
        id[0] = label[0] = '\0';
        attribute(cursor, end, "id", id, sizeof(id));
        attribute(cursor, end, "text", label, sizeof(label));
        if (id[0] && label[0] && action_token("source:", id, action, sizeof(action)))
            if (!append_action(writer, action, label, "source")) return;
        cursor = end + 1;
    }
}

static void append_presets(json_writer_t *writer, const char *xml)
{
    const char *cursor = xml;
    char id[96], label[160], action[320];
    while ((cursor = next_element(cursor, "preset", NULL)) != NULL) {
        const char *end = find_char(cursor, '>');
        if (!end) return;
        id[0] = label[0] = '\0';
        attribute(cursor, end, "id", id, sizeof(id));
        attribute(cursor, end, "name", label, sizeof(label));
        if (id[0] && label[0] && action_token("preset:", id, action, sizeof(action)))
            if (!append_action(writer, action, label, "preset")) return;
        cursor = end + 1;
    }
}

static uint8_t refresh_status(void)
{
    char *response = s_host->alloc(RESPONSE_MAX);
    if (!response) return 0;
    json_writer_t writer = {.data = s_status_cache,
        .size = sizeof(s_status_cache), .length = 0, .first = 1, .valid = 1};
    append_text(&writer, "{\"capabilities_source\":\"player\",\"actions\":[");
    for (size_t i = 0; i < sizeof(s_actions) / sizeof(s_actions[0]); i++) {
        if (i >= 4 && i <= 6) continue;
        append_action(&writer, s_actions[i].name, s_actions[i].label,
                      s_actions[i].category);
    }
    size_t length = 0;
    if (request_path("/RadioBrowse?service=Capture", response, RESPONSE_MAX,
                     &length) == KW_PLUGIN_STATUS_OK) {
        append_sources(&writer, response);
    } else {
        for (size_t i = 4; i <= 6; i++)
            append_action(&writer, s_actions[i].name, s_actions[i].label,
                          s_actions[i].category);
    }
    length = 0;
    if (request_path("/Presets", response, RESPONSE_MAX, &length) ==
        KW_PLUGIN_STATUS_OK)
        append_presets(&writer, response);
    append_char(&writer, ']');
    length = 0;
    if (request_path("/SyncStatus", response, RESPONSE_MAX, &length) ==
        KW_PLUGIN_STATUS_OK)
        append_player(&writer, response);
    append_char(&writer, '}');
    s_host->free(response);
    s_status_cache_time = s_host->monotonic_ms();
    return writer.valid && writer.length > 2 && writer.length < writer.size - 1;
}

static char *status_json(void)
{
    uint64_t now = s_host->monotonic_ms();
    if (!s_status_cache[0] || !s_status_cache_time ||
        now - s_status_cache_time > STATUS_CACHE_MS)
        if (!refresh_status()) return NULL;
    size_t length = text_length(s_status_cache) + 1;
    char *copy = s_host->alloc(length);
    if (!copy) return NULL;
    memcpy(copy, s_status_cache, length);
    return copy;
}

static kw_plugin_connection_t connection_state(void)
{
    char host[64]; uint32_t port;
    return s_started && target(host, &port) == KW_PLUGIN_STATUS_OK
        ? KW_PLUGIN_CONNECTION_CONNECTED : KW_PLUGIN_CONNECTION_DISCONNECTED;
}

static const kw_plugin_descriptor_v1_t s_descriptor = {
    .magic = KW_PLUGIN_ABI_MAGIC,
    .struct_size = sizeof(kw_plugin_descriptor_v1_t),
    .required_abi_major = KW_PLUGIN_ABI_MAJOR,
    .required_abi_minor = 4u,
    .id = PLUGIN_ID, .display_name = "BluOS", .version = "0.1.4",
    .tier = KW_PLUGIN_TIER_PREVIEW,
    .bind = bind_host, .initialize = initialize, .start = start,
    .stop = stop, .deinitialize = deinitialize,
    .validate_config = validate_config, .execute_action = execute_action,
    .get_status_json = status_json, .get_connection_state = connection_state,
    .actions = s_actions,
    .action_count = sizeof(s_actions) / sizeof(s_actions[0]),
    .settings = s_settings,
    .setting_count = sizeof(s_settings) / sizeof(s_settings[0]),
    .discovery_profile = "bluos",
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
