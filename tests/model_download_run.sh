#!/bin/sh
# Run inside the native Docker build image.
set -eu
mkdir -p dist/model-tests
clang-18 -std=c11 -O1 -g -fsanitize=address,undefined -Isrc -c src/client_sha256.c -o dist/model-tests/sha.o
clang++-18 -std=c++20 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc -Inative/src \
  native/src/model_download.cpp tests/model_download_test.cpp dist/model-tests/sha.o -o dist/model-tests/download-test
./dist/model-tests/download-test
python3 tests/local_configure_test.py
clang++-18 -std=c++20 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc -Inative/src \
  native/src/model_download.cpp native/src/model_https.cpp tests/model_https_test.cpp dist/model-tests/sha.o -o dist/model-tests/https-test
./dist/model-tests/https-test
if test -f assets/inference/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf; then
  ./dist/model-tests/download-test assets/inference/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf
fi
python3 tests/model_assets_test.py
