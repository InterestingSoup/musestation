#!/bin/sh
set -eu
mkdir -p dist
clang-18 -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc \
 src/local_job.c src/config.c src/json.c src/client.c src/client_sha256.c src/log.c tests/local_job_test.c -o dist/local-job-test
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT
./dist/local-job-test "$temp"
/opt/ps5-payload-sdk/bin/prospero-clang -std=c11 -O2 -Wall -Wextra -Werror -Isrc -c src/local_job.c -o dist/local_job.o
