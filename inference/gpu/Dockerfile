FROM ps5-muse-native
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3-pip python3-mako python3-yaml python3-packaging bison flex \
    libclang-18-dev libclc-18-dev libllvmspirvlib-18-dev libelf-dev \
    libvulkan-dev glslc glslang-tools spirv-headers spirv-tools zlib1g-dev \
    && pip3 install --break-system-packages meson==1.9.1 \
    && rm -rf /var/lib/apt/lists/*
ENV CC=clang-18 CXX=clang++-18 LLVM_CONFIG=/usr/bin/llvm-config-18 BUILD_JOBS=1 MUSE_BUILD_JOBS=1
WORKDIR /work
