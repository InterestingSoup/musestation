#!/bin/sh
set -eu
mkdir -p dist
clang-18 -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc \
 src/client.c src/client_sha256.c src/config.c src/json.c tests/client_inventory_test.c -o dist/client-inventory-test
python3 tests/client_inventory_fixture.py ./dist/client-inventory-test
