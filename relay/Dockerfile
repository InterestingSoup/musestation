FROM ubuntu:24.04@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac
ENV DEBIAN_FRONTEND=noninteractive PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
RUN apt-get update && apt-get install -y --no-install-recommends python3 ca-certificates docker.io \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . /app/relay
USER 1000:1000
CMD ["python3", "-m", "relay.server"]
