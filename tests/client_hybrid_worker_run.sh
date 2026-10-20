#!/bin/sh
set -eu
# Uses freshly compiled sanitized objects from tests/client_run.sh.
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fPIC -shared \
  tests/client_hybrid_audio_mock.c -o dist/hybrid-audio-mock.so
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
  -c src/gate.c -o dist/host-gate.o
clang++-18 -std=c++20 -Wall -Wextra -Werror -Wno-unused-private-field -O1 -g -fsanitize=address,undefined \
  -DMUSE_LOCAL_INFERENCE -Isrc -Inative/src -Iinference -Ivendor/native-ui/src -pthread \
  native/src/worker.cpp tests/client_hybrid_worker_test.cpp \
  dist/host-client.o dist/host-client_install.o dist/host-client_install_paths.o dist/host-client_install_fds.o \
  dist/host-client_sha256.o dist/host-config.o dist/host-json.o dist/host-log.o dist/host-local_job.o dist/host-gate.o \
  build/local-app/host/libmuse-local-app.a -lm -ldl -o dist/hybrid-worker-test
python3 tests/client_hybrid_worker_fixture.py ./dist/hybrid-worker-test ./dist/hybrid-audio-mock.so
