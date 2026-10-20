#!/bin/sh
# Run inside the pinned Linux SDK container. Output is an ELF with an asset extension.
set -eu
sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
output=${1:-build/native/muse/assets/install/muse-install-helper.payload}
mkdir -p "$(dirname "$output")"
"$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -DMUSE_INSTALL_PAYLOAD_HELPER -Isrc -Inative/install-helper \
  native/install-helper/main.c native/install-helper/appinst_runtime.c src/client.c src/client_install_paths.c src/client_install_fds.c src/client_sha256.c src/config.c src/json.c src/log.c \
  -lSceSystemService -lSceUserService -lSceNet -lSceNetCtl -lpthread -o "$output"
