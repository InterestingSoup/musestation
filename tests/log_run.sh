#!/bin/sh
set -eu
mkdir -p dist
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
  src/config.c src/json.c src/log.c tests/log_recovery_test.c -Wl,--wrap=fsync -o dist/log-recovery-test
temporary=$(mktemp -d /tmp/muse-log-test.XXXXXX)
trap 'rm -rf "$temporary"' EXIT
./dist/log-recovery-test "$temporary"
/opt/ps5-payload-sdk/bin/prospero-clang -std=c11 -Wall -Wextra -Werror -O2 -Isrc -c src/log.c -o dist/log-recovery-target.o
