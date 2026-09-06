# Host security regressions

Run from the plugin repository root with a C11 compiler:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
  -fno-sanitize-recover=undefined tests/test_sonos_authority.c \
  sonos/main/sonos.c -o /tmp/test_sonos_authority
/tmp/test_sonos_authority
```

This links the public module source as a separate translation unit against a
fake SDK host. It verifies requests use only the official HTTPS authority and
that changed host/port settings neither read the retained token nor dispatch a
request. It does not contact Sonos, sign a package, or prove device connectivity.
Run the ESP-IDF 6.0.2 `so` target and SDK release validator before release.

The CI workflow also runs `test_samsung_transport.c` linked with
`samsung/main/samsung.c` using the same compiler flags. It injects a TLS
identity failure and checks that a retained token causes no WS fallback,
that explicit WS and certificate-name bypass are rejected with a token, and
that an unset transport uses verified WSS. This is host callback evidence;
it does not establish certificate enrollment or TV compatibility. The core
still needs endpoint-bound credential update policy. Previously saved
`tls_skip=1` settings with a token are intentionally rejected.
