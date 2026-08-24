// SPDX-License-Identifier: Apache-2.0

#include "../../sdk/include/kw_module_helpers.h"

#define PLUGIN_ID "pjlink"

typedef struct {
    const char *action;
    const char *command;
    const char *parameter;
    uint8_t command_class;
} action_map_t;

typedef struct {
    char command[48];
    char password[64];
} prepare_context_t;

static const kw_plugin_host_api_v1_t *s_host;
static uint8_t s_started;

static const kw_plugin_action_descriptor_v1_t s_actions[] = {
    {"power_on", "Power On", "power"},
    {"power_off", "Power Off", "power"},
    {"status_query", "Power Status", "power"},
    {"input_rgb1", "RGB 1", "input"},
    {"input_hdmi1", "HDMI 1", "input"},
    {"input_hdmi2", "HDMI 2", "input"},
    {"av_mute_on", "AV Mute On", "mute"},
    {"av_mute_off", "AV Mute Off", "mute"},
    {"freeze_on", "Freeze On", "display"},
    {"freeze_off", "Freeze Off", "display"},
};

static const kw_plugin_setting_descriptor_v1_t s_settings[] = {
    {
        .key = "host",
        .label = "Projector IP address or hostname",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_REQUIRED,
        .default_string = "",
        .maximum_length = 63,
    },
    {
        .key = "port",
        .label = "PJLink TCP port",
        .type = KW_PLUGIN_SETTING_U32,
        .default_u32 = 4352,
        .minimum_u32 = 1,
        .maximum_u32 = 65535,
    },
    {
        .key = "password",
        .label = "PJLink password",
        .type = KW_PLUGIN_SETTING_STRING,
        .flags = KW_PLUGIN_SETTING_SECRET,
        .default_string = "",
        .maximum_length = 63,
    },
};

static const action_map_t s_action_map[] = {
    {"power_on", "POWR", "1", 1},
    {"power_off", "POWR", "0", 1},
    {"status_query", "POWR", "?", 1},
    {"input_rgb1", "INPT", "11", 1},
    {"input_hdmi1", "INPT", "31", 1},
    {"input_hdmi2", "INPT", "32", 1},
    {"av_mute_on", "AVMT", "31", 1},
    {"av_mute_off", "AVMT", "30", 1},
    {"freeze_on", "FREZ", "1", 2},
    {"freeze_off", "FREZ", "0", 2},
};

static kw_plugin_status_t target(char host[64],
                                 uint32_t *port,
                                 char password[64])
{
    kw_plugin_status_t result =
        kw_setting_string(s_host, PLUGIN_ID, "host", host, 64, 1);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    result =
        kw_setting_u32(s_host, PLUGIN_ID, "port", 4352, 1, 65535, port);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    return kw_setting_string(
        s_host, PLUGIN_ID, "password", password, 64, 0);
}

static uint8_t build_command(const char *action,
                             char *output,
                             size_t output_size)
{
    for (size_t i = 0; i < sizeof(s_action_map) / sizeof(s_action_map[0]); i++) {
        if (!kw_string_equal(action, s_action_map[i].action)) continue;
        size_t offset = 0;
        output[0] = '\0';
        return kw_append_char(output, output_size, &offset, '%') &&
               kw_append_u32(output, output_size, &offset,
                             s_action_map[i].command_class) &&
               kw_append(output, output_size, &offset,
                         s_action_map[i].command) &&
               kw_append_char(output, output_size, &offset, ' ') &&
               kw_append(output, output_size, &offset,
                         s_action_map[i].parameter) &&
               kw_append_char(output, output_size, &offset, '\r');
    }
    return 0;
}

static uint8_t preface_starts_with(const uint8_t *preface,
                                   size_t preface_length,
                                   const char *text)
{
    size_t index = 0;
    while (text[index]) {
        if (index >= preface_length ||
            preface[index] != (uint8_t)text[index]) {
            return 0;
        }
        index++;
    }
    return 1;
}

static kw_plugin_status_t prepare_write(
    const uint8_t *preface,
    size_t preface_length,
    uint8_t *write_buffer,
    size_t write_buffer_size,
    size_t *write_length,
    void *context)
{
    prepare_context_t *prepare = (prepare_context_t *)context;
    size_t offset = 0;
    if (!prepare || !preface || !write_buffer || !write_length) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    if (preface_starts_with(preface, preface_length, "PJLINK 0")) {
        if (!kw_append((char *)write_buffer, write_buffer_size, &offset,
                       prepare->command)) {
            return KW_PLUGIN_STATUS_INTERNAL_ERROR;
        }
        *write_length = offset;
        return KW_PLUGIN_STATUS_OK;
    }
    if (!preface_starts_with(preface, preface_length, "PJLINK 1 ") ||
        preface_length < 17u || !prepare->password[0]) {
        return KW_PLUGIN_STATUS_AUTH_FAILED;
    }

    uint8_t challenge_and_password[72];
    for (size_t i = 0; i < 8u; i++) {
        uint8_t value = preface[9u + i];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f') ||
              (value >= 'A' && value <= 'F'))) {
            return KW_PLUGIN_STATUS_AUTH_FAILED;
        }
        challenge_and_password[i] = value;
    }
    size_t password_length = kw_string_length(prepare->password);
    if (password_length > 63u) return KW_PLUGIN_STATUS_AUTH_FAILED;
    for (size_t i = 0; i < password_length; i++) {
        challenge_and_password[8u + i] = (uint8_t)prepare->password[i];
    }

    uint8_t digest[16];
    char digest_hex[33];
    if (!kw_md5(challenge_and_password, 8u + password_length, digest) ||
        !kw_hex_lower(digest, sizeof(digest), digest_hex,
                      sizeof(digest_hex)) ||
        !kw_append((char *)write_buffer, write_buffer_size, &offset,
                   digest_hex) ||
        !kw_append((char *)write_buffer, write_buffer_size, &offset,
                   prepare->command)) {
        return KW_PLUGIN_STATUS_INTERNAL_ERROR;
    }
    *write_length = offset;
    return KW_PLUGIN_STATUS_OK;
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
    char password[64];
    uint32_t port;
    return target(host, &port, password);
}

static kw_plugin_status_t execute_action(const char *action)
{
    char host[64];
    uint32_t port;
    prepare_context_t prepare;
    kw_plugin_status_t result = target(host, &port, prepare.password);
    if (result != KW_PLUGIN_STATUS_OK) return result;
    if (!build_command(action, prepare.command, sizeof(prepare.command))) {
        return KW_PLUGIN_STATUS_UNSUPPORTED;
    }

    uint8_t preface[64];
    size_t preface_length = 0;
    uint8_t response[128];
    size_t response_length = 0;
    kw_plugin_tcp_request_v1_t request;
    kw_tcp_request_init(&request);
    request.host = host;
    request.port = (uint16_t)port;
    request.connect_timeout_ms = 3000;
    request.read_timeout_ms = 3000;
    request.read_buffer = response;
    request.read_buffer_size = sizeof(response);
    request.read_length = &response_length;
    request.preface_buffer = preface;
    request.preface_buffer_size = sizeof(preface);
    request.preface_length = &preface_length;
    request.prepare_write = prepare_write;
    request.prepare_context = &prepare;
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
    .display_name = "PJLink",
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
    .discovery_profile = "pjlink",
};

const kw_plugin_descriptor_v1_t *klangwehr_plugin_entry(void)
{
    return &s_descriptor;
}
