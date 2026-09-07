FROM ubuntu:24.04

RUN apt-get update -qq \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq wrk curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY client/run.sh /work/run.sh
RUN chmod +x /work/run.sh

ENTRYPOINT ["/work/run.sh"]
