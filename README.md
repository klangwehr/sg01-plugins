# Official SG/01 Plugins

This repository contains Klangwehr-maintained native integrations for
Signalgerät SG/01. Each first-level directory is an independently versioned
ESP32-S3 shared object. The firmware core is not included here.

The `sdk` submodule pins the exact public SDK revision used by this repository.
Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/klangwehr/sg01-plugins.git
```

## Plugin lifecycle

Each module is independently built, validated, signed, installed, enabled,
disabled, and removed. A device downloads only the integrations selected by
its owner. The descriptor supplies its actions and typed settings, so a
compatible plugin needs no core or web UI changes.

`hello_world` is a non-published ABI fixture. Apple TV remains a built-in core
integration because its long-lived encrypted pairing and session lifecycle is
outside the safe public ABI.

## Build one module

```sh
cd sonos
idf.py -B build -G "Unix Makefiles" set-target esp32s3
idf.py -B build -G "Unix Makefiles" so
python ../sdk/tools/plugin_release.py validate \
  --module module.json --elf build/sonos.so
```

Use ESP-IDF 6.0.2. The CI workflow builds every module in a separate matrix
job and never receives catalog, signing, or cloud credentials.

## Releases

An approved merge to `staging` is eligible for a signed `rc` package.
An approved promotion from `staging` to `main` is eligible for `stable`.
Package versions are immutable; change a module or the SDK submodule and bump
both the module metadata and exported descriptor version.

## Support status

All third-party integrations start as **Official integration — Preview**.
Their published documentation must list hardware and third-party software
tested, the functions verified, known limitations, and a last verification
date. Preview is the customer-facing status for integrations that depend on
third-party device behaviour.

See the [SDK developer guide](https://github.com/klangwehr/sg01-plugin-sdk/blob/main/docs/developer-guide.md)
for ABI, security, packaging, and Developer Mode requirements.
