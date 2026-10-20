#!/bin/sh
# Run in the pinned SDK Docker image, never the Mac host.
set -eu
mkdir -p dist
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -D_DEFAULT_SOURCE \
  -DMUSE_INSTALL_HELPER_TEST -DMUSE_INSTALL_OBSERVE_MS=150 -Isrc -Inative/install-helper \
  native/install-helper/main.c native/install-helper/appinst_runtime.c tests/client_install_native_mock.c \
  src/client.c src/client_install_paths.c src/client_install_fds.c src/client_sha256.c src/config.c src/json.c src/log.c -pthread -o dist/install-helper-runtime-test
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
  tests/client_install_runtime_test.c src/client.c src/client_install.c src/client_install_paths.c src/client_install_fds.c src/client_sha256.c src/config.c src/json.c src/log.c -o dist/install-runtime-test
python3 tests/client_install_runtime_fixture.py ./dist/install-runtime-test ./dist/install-helper-runtime-test
sh native/install-helper/build.sh dist/muse-install-helper.payload
python3 - <<'PY'
import hashlib, pathlib, re, subprocess
p = pathlib.Path('dist/muse-install-helper.payload')
dynamic = subprocess.check_output(['llvm-readelf-18', '-d', str(p)], text=True)
symbols = subprocess.check_output(['llvm-readelf-18', '--dyn-syms', '--wide', str(p)], text=True)
assert not any('NEEDED' in line and 'AppInstUtil' in line for line in dynamic.splitlines()), dynamic
assert not any(re.search(r'\bUND\b', line) and 'sceAppInstUtil' in line for line in symbols.splitlines()), symbols
for name in ['sceKernelLoadStartModule', 'kernel_dynlib_dlsym']:
    assert name in subprocess.check_output(['llvm-nm-18', str(p)], text=True), name
data = p.read_bytes()
assert data.count(b'MUSE_INSTALL_BOOTSTRAP_v1_4be21'.ljust(32, b'\0')) == 1
print('Native helper: no AppInstUtil DT_NEEDED or undefined imports; SDK payload resolver present; bootstrap marker count 1')
print('Helper bytes:', len(data), 'SHA256:', hashlib.sha256(data).hexdigest())
PY
