FROM ps5-muse-toolchain
RUN apt-get update && apt-get install -y --no-install-recommends clang-format-18 curl unzip rsync ccache libegl1-mesa-dev libgl-dev libgl1-mesa-dri python3-pil libssl-dev pkg-config && rm -rf /var/lib/apt/lists/*
ENV CC=clang-18 CXX=clang++-18 HOST_CC=clang-18 HOST_CXX=clang++-18 BUILD_JOBS=2
WORKDIR /work
