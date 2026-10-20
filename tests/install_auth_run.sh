#!/bin/sh
set -eu
mkdir -p dist
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -D_DEFAULT_SOURCE \
  -DMUSE_INSTALL_HELPER_TEST -DMUSE_INSTALL_OBSERVE_MS=150 -Isrc -Inative/install-helper \
  native/install-helper/main.c native/install-helper/appinst_runtime.c tests/client_install_native_mock.c \
  src/client.c src/client_install_paths.c src/client_install_fds.c src/client_sha256.c src/config.c src/json.c src/log.c -pthread -o dist/install-helper-test
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
  tests/client_install_test.c src/client.c src/client_install.c src/client_install_paths.c src/client_install_fds.c src/client_sha256.c src/config.c src/json.c src/log.c -o dist/install-test
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
  tests/client_install_fds_test.c src/client.c src/client_install_fds.c src/client_sha256.c src/config.c src/json.c src/log.c -o dist/install-fds-test
python3 tests/client_install_fixture.py ./dist/install-test ./dist/install-helper-test ./dist/install-fds-test
sh native/install-helper/build.sh dist/muse-install-helper.payload
