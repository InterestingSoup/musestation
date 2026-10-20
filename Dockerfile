FROM ubuntu:24.04@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-18 libclang-rt-18-dev lld-18 llvm-18-dev make ca-certificates python3 python3-pyelftools \
    socat cmake ninja-build meson pkg-config git wget file && rm -rf /var/lib/apt/lists/*
ENV LLVM_CONFIG=/usr/bin/llvm-config-18 PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
ENV PATH=/opt/ps5-payload-sdk/bin:$PATH
COPY vendor/sdk /src/sdk
RUN make -C /src/sdk DESTDIR=/opt/ps5-payload-sdk install
RUN make -C /src/sdk/samples/hello_world
WORKDIR /work
CMD ["make", "all"]
