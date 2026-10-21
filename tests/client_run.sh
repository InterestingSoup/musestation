#!/bin/sh
set -eu
# Run INSIDE ps5-muse-toolchain, not the Mac host.
mkdir -p dist
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
  src/client.c src/client_sha256.c src/config.c src/json.c src/log.c src/gate.c tests/client_test.c \
  -o dist/client-test
for unit in client client_sha256 client_install client_install_paths client_install_fds config json log local_job; do
  clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
    -c "src/$unit.c" -o "dist/host-$unit.o"
done
clang++-18 -std=c++20 -Wall -Wextra -Werror -Wno-unused-private-field -O1 -g -fsanitize=address,undefined \
  -Isrc -Inative/src -Ivendor/native-ui/src -pthread native/src/worker.cpp native/src/model_download.cpp tests/no_model_network.cpp tests/client_worker_test.cpp \
  dist/host-client.o dist/host-client_install.o dist/host-client_install_paths.o dist/host-client_install_fds.o dist/host-client_sha256.o dist/host-config.o dist/host-json.o dist/host-log.o dist/host-local_job.o build/local-app/host/libmuse-local-app.a \
  -lm -ldl -o dist/worker-test
python3 tests/client_fixture.py ./dist/client-test ./dist/worker-test
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
  src/client.c src/client_sha256.c src/config.c src/json.c tests/client_pending_sync_test.c \
  -Wl,--wrap=fsync -o dist/pending-sync-test
python3 tests/client_pending_sync_fixture.py ./dist/pending-sync-test
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc -pthread \
  src/client_phone.c src/config.c src/json.c tests/client_phone_test.c -o dist/phone-test
python3 tests/client_phone_fixture.py ./dist/phone-test
for unit in client client_sha256 client_install client_install_paths client_install_fds client_phone config json log local_job; do
  /opt/ps5-payload-sdk/bin/prospero-clang -std=c11 -Wall -Wextra -Werror -O2 -g -Isrc \
    -c "src/$unit.c" -o "dist/$unit.o"
done

clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -D_DEFAULT_SOURCE \
  -DMUSE_INSTALL_HELPER_TEST -DMUSE_INSTALL_OBSERVE_MS=150 -Isrc -Inative/install-helper \
  native/install-helper/main.c native/install-helper/appinst_runtime.c tests/client_install_native_mock.c \
  src/client.c src/client_install_paths.c src/client_install_fds.c src/client_sha256.c src/config.c src/json.c src/log.c -pthread -o dist/install-helper-test
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
  tests/client_install_test.c src/client.c src/client_install.c src/client_install_paths.c src/client_install_fds.c src/client_sha256.c src/config.c src/json.c src/log.c -o dist/install-test
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
  tests/client_install_fds_test.c src/client.c src/client_install_fds.c src/client_sha256.c src/config.c src/json.c src/log.c -o dist/install-fds-test
python3 tests/client_install_fixture.py ./dist/install-test ./dist/install-helper-test ./dist/install-fds-test
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc tests/client_install_paths_test.c src/client_install_paths.c -o dist/install-paths-test
./dist/install-paths-test
sh native/install-helper/build.sh dist/muse-install-helper.payload
clang-18 -std=c11 -Wall -Wextra -Werror -O2 -Isrc tests/client_verify_pkg.c src/client.c src/client_sha256.c src/config.c src/json.c -o dist/verify-pkg
