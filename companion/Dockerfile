FROM ubuntu:24.04@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac
ENV DEBIAN_FRONTEND=noninteractive PYTHONDONTWRITEBYTECODE=1
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 python3-venv python3-cryptography python3-dbus python3-gi python3-pytest ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY vendor/muse-gadget-sdk/linux/src/musegadget/data/requirements.lock /tmp/websockets.lock
RUN python3 -m venv --system-site-packages /opt/venv && \
    /opt/venv/bin/pip install --no-cache-dir --require-hashes -r /tmp/websockets.lock
COPY . /app
WORKDIR /app
ENV PYTHONPATH=/app/src:/app/vendor/muse-gadget-sdk/linux/src
RUN /opt/venv/bin/python verify_vendor.py
USER 65534:65534
ENTRYPOINT ["/opt/venv/bin/python", "-m", "musestation_companion"]
CMD ["--state-dir", "/state", "status"]
