#!/bin/sh
set -eu
# tests/client_run.sh builds the sanitized C objects and real runtime archive exists.
clang++-18 -std=c++20 -Wall -Wextra -Werror -Wno-unused-private-field -O1 -g -fsanitize=address,undefined \
  -DMUSE_LOCAL_INFERENCE -Isrc -Inative/src -Iinference -Ivendor/native-ui/src -pthread \
  native/src/worker.cpp native/src/model_download.cpp tests/no_model_network.cpp tests/local_worker_test.cpp \
  dist/host-client.o dist/host-client_install.o dist/host-client_install_paths.o dist/host-client_install_fds.o \
  dist/host-client_sha256.o dist/host-config.o dist/host-json.o dist/host-log.o dist/host-local_job.o \
  build/local-app/host/libmuse-local-app.a -lm -ldl -o dist/local-worker-test
python3 tests/local_worker_fixture.py ./dist/local-worker-test
