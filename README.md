# telegram-dm-notif — Facet plugin

Unread messages from your **private Telegram chats** on the panel: who wrote,
how many messages and the last one. Runs as an out-of-process plugin of
[facet-core](https://github.com/siakinnik/facet-core).

Only notifications: a full Telegram client for the panel is planned as a
separate module once Facet can run desktop (Wayland) apps.

## How it works

The plugin signs in to your account as a regular Telegram client app, like
Telegram Desktop, through [TDLib](https://github.com/tdlib/td), Telegram's
official client library, linked statically. It is **read only**:

- it never sends messages and never marks anything as read, so your phone
  still shows everything as unread;
- a chat leaves the list as soon as you read it in any Telegram app;
- groups, channels, Saved Messages and (by default) muted chats and bots are
  not shown;
- Telegram's per-chat "show preview" setting is respected, and texts from the
  Telegram service account (login codes) are never shown;
- "Show message text" off shows only "Photo", "New message", ...

The Telegram session lives in `/var/lib/facet/data/telegram-dm-notif/tdlib`. Anyone who
can read that directory can use the session: keep the device's root account
safe. "Sign out" on the plugin screen, or Telegram → Settings → Devices on the
phone, ends it.

## Setup

1. Telegram requires every client app to have its own API ID. On a phone or
   computer open [my.telegram.org](https://my.telegram.org), sign in, choose
   **API development tools** and create an app (any name and short name,
   platform "Desktop"). Keep `api_id` and `api_hash` at hand.
2. On the panel allow the `network` permission when Facet asks (Settings >
   Apps > Telegram messages), open its tile and enter `api_id` and `api_hash`.
3. Sign in: **Sign in with a QR code** and scan it with the phone
   (Telegram → Settings → Devices → Link Desktop Device), or with the phone
   number and the code Telegram sends. If the account has two-step
   verification, the password is typed on the core's built-in keyboard
   (keyboard plugins never see it) and kept in memory only until it is handed
   to TDLib.

Using the official API through TDLib is what Telegram's API terms allow for
third-party clients; the plugin does no automation and sends nothing.

## Install

On a device that already runs [Facet](https://github.com/siakinnik/facet-core)
(prebuilt static binary for x86_64):

```bash
curl -fsSL https://raw.githubusercontent.com/siakinnik/facet-core/main/scripts/get.sh | sudo bash -s -- --plugin siakinnik/facet-telegram-dm-notif
```

Run the same command to update. Remove with `… | sudo bash -s -- --remove-plugin telegram-dm-notif`
(this does not end the Telegram session; sign out first or end it from the phone).

## Build from source

TDLib is part of the plugin: the first configure builds the commit pinned in
`TDLIB_COMMIT` into `third_party/tdlib` (20–60 minutes, a few GB of RAM, once)
and the plugin links it statically together with OpenSSL and zlib. System-wide
TDLib installs are ignored, and the resulting binary needs nothing outside its
own directory.

```bash
sudo apt install cmake ninja-build g++ gperf libssl-dev zlib1g-dev
cmake -S . -B build -G Ninja -DTG_STATIC=ON && cmake --build build
ctest --test-dir build
FACET_PLUGIN_PATH=$PWD/build ../facet-core/build/facet   # run with a locally built core
```

The plugin SDK comes from facet-core: a checkout in `../facet-core` (or
`-DFACET_CORE_DIR=…`) is used when present, otherwise CMake downloads it at
`FACET_CORE_REF`. QR codes use [nayuki/QR-Code-generator](https://github.com/nayuki/QR-Code-generator)
(MIT), downloaded by CMake. `-DTG_STATIC=ON` links a fully static binary.

## Releases

CI caches `third_party/tdlib` per pinned commit, then checks translations and
versions, builds, runs the unit tests and a protocol smoke test. To release,
bump the version in `CMakeLists.txt` (`project(VERSION)`, `TG_VERSION_SUFFIX`)
and `manifest.json`, push, then Actions → Release → *Run workflow*. Releases
are x86_64 only for now: ARM needs TDLib cross-compiled with the target's
OpenSSL and zlib.

To move to a newer TDLib, put its commit into `TDLIB_COMMIT`.

## Files

```
src/td_client.*   TDLib JSON interface: receiver thread and queue
src/inbox.*       unread private chats from TDLib updates (unit-tested)
src/main.cpp      plugin glue: sign-in flow, QR code, message list, tile
src/i18n/         translations
tests/            unit tests with TDLib-shaped updates
```
