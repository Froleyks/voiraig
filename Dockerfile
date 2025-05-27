FROM ubuntu:24.04

RUN \
  apt-get update && \
    apt-get install -y --no-install-recommends \
    cmake make g++ git && \
  apt-get clean

COPY . /workdir
WORKDIR /workdir
RUN \
    make clean && \
    make -j$(nproc) -C build

# ENTRYPOINT ["/workdir/bin/"]
