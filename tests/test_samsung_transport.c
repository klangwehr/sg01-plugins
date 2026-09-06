// SPDX-License-Identifier: Apache-2.0
#include "../sdk/include/kw_plugin_abi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *scheme = "auto";
static uint32_t tls_skip;
static unsigned requests;
static kw_plugin_status_t get_string(const char *id, const char *key,
                                    char *out, size_t size)
{
    assert(!strcmp(id, "samsung"));
    const char *value = "";
    if (!strcmp(key, "host")) value = "tv.example";
    else if (!strcmp(key, "scheme")) value = scheme;
    else if (!strcmp(key, "token")) value = "sentinel-token";
    assert(strlen(value) < size);
    strcpy(out, value);
    return KW_PLUGIN_STATUS_OK;
}
static kw_plugin_status_t get_u32(const char *id, const char *key, uint32_t *out)
{
    assert(!strcmp(id, "samsung"));
    if (!strcmp(key, "tls_skip")) { *out = tls_skip; return KW_PLUGIN_STATUS_OK; }
    return KW_PLUGIN_STATUS_NOT_CONFIGURED;
}
static kw_plugin_status_t set_string(const char *id, const char *key, const char *value)
{
    (void)id; (void)key; (void)value;
    assert(0 && "failed transport must not change token");
    return KW_PLUGIN_STATUS_INTERNAL_ERROR;
}
static kw_plugin_status_t exchange(const kw_plugin_websocket_request_v1_t *req)
{
    requests++;
    assert(!strncmp(req->uri, "wss://tv.example:8002/", 22));
    assert(!req->skip_certificate_name_check);
    /* Simulate TLS identity failure: there must be no subsequent WS attempt. */
    return KW_PLUGIN_STATUS_AUTH_FAILED;
}
int main(void)
{
    kw_plugin_host_api_v1_t host = {0};
    host.struct_size = sizeof(host);
    host.abi_major = KW_PLUGIN_ABI_MAJOR;
    host.abi_minor = KW_PLUGIN_ABI_MINOR;
    host.setting_get_string = get_string;
    host.setting_set_string = set_string;
    host.setting_get_u32 = get_u32;
    host.websocket_exchange = exchange;
    const kw_plugin_descriptor_v1_t *plugin = klangwehr_plugin_entry();
    assert(plugin->bind(&host) == KW_PLUGIN_STATUS_OK);
    assert(plugin->execute_action("play") == KW_PLUGIN_STATUS_AUTH_FAILED);
    assert(requests == 1);
    scheme = "ws";
    assert(plugin->execute_action("play") == KW_PLUGIN_STATUS_NOT_CONFIGURED);
    assert(requests == 1);
    scheme = "wss";
    tls_skip = 1;
    assert(plugin->execute_action("play") == KW_PLUGIN_STATUS_NOT_CONFIGURED);
    assert(requests == 1);
    tls_skip = 0;
    scheme = "";
    assert(plugin->execute_action("play") == KW_PLUGIN_STATUS_AUTH_FAILED);
    assert(requests == 2);
    puts("PASS: retained Samsung token never falls back to WS or bypasses certificate name");
    return 0;
}
