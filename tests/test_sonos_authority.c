// SPDX-License-Identifier: Apache-2.0
#include "../sdk/include/kw_plugin_abi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *configured_host = "api.ws.sonos.com";
static uint32_t configured_port = 443;
static unsigned requests, token_reads;

static kw_plugin_status_t get_string(const char *id, const char *key,
                                    char *out, size_t size)
{
    assert(strcmp(id, "sonos") == 0);
    const char *value = "";
    if (!strcmp(key, "host")) value = configured_host;
    else if (!strcmp(key, "access_token")) { value = "sentinel-token"; token_reads++; }
    else if (!strcmp(key, "group_id")) value = "test-group";
    assert(strlen(value) < size);
    strcpy(out, value);
    return KW_PLUGIN_STATUS_OK;
}
static kw_plugin_status_t get_u32(const char *id, const char *key, uint32_t *out)
{
    assert(strcmp(id, "sonos") == 0);
    if (!strcmp(key, "port")) { *out = configured_port; return KW_PLUGIN_STATUS_OK; }
    return KW_PLUGIN_STATUS_NOT_CONFIGURED;
}
static kw_plugin_status_t http_request(const kw_plugin_http_request_v1_t *req)
{
    requests++;
    assert(!strcmp(req->host, "api.ws.sonos.com"));
    assert(req->port == 443 && req->use_tls);
    assert(req->header_count >= 1);
    assert(!strcmp(req->headers[0].name, "Authorization"));
    assert(!strcmp(req->headers[0].value, "Bearer sentinel-token"));
    return KW_PLUGIN_STATUS_OK;
}
int main(void)
{
    kw_plugin_host_api_v1_t host = {0};
    host.struct_size = sizeof(host);
    host.abi_major = KW_PLUGIN_ABI_MAJOR;
    host.abi_minor = KW_PLUGIN_ABI_MINOR;
    host.setting_get_string = get_string;
    host.setting_get_u32 = get_u32;
    host.http_request = http_request;
    const kw_plugin_descriptor_v1_t *plugin = klangwehr_plugin_entry();
    assert(plugin->bind(&host) == KW_PLUGIN_STATUS_OK);
    assert(plugin->execute_action("play") == KW_PLUGIN_STATUS_OK);
    assert(requests == 1 && token_reads == 1);
    const char *invalid[] = {"attacker.invalid", "api.ws.sonos.com.attacker.invalid",
        "api.ws.sonos.com@attacker.invalid", "https://api.ws.sonos.com", "127.0.0.1"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        configured_host = invalid[i];
        assert(plugin->validate_config() == KW_PLUGIN_STATUS_NOT_CONFIGURED);
        assert(plugin->execute_action("play") == KW_PLUGIN_STATUS_NOT_CONFIGURED);
        assert(requests == 1 && token_reads == 1);
    }
    configured_host = "api.ws.sonos.com";
    configured_port = 8443;
    assert(plugin->execute_action("play") == KW_PLUGIN_STATUS_NOT_CONFIGURED);
    assert(requests == 1 && token_reads == 1);
    configured_port = 443;
    configured_host = "";
    assert(plugin->execute_action("play") == KW_PLUGIN_STATUS_OK);
    assert(requests == 2);
    puts("PASS: retained Sonos token never reaches a changed authority");
    return 0;
}
