FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    clang-18 \
    lld-18 \
    make \
    curl \
    unzip \
    python3 \
    && rm -rf /var/lib/apt/lists/*

# Configure LLVM aliases
RUN ln -s /usr/bin/clang-18 /usr/local/bin/clang && \
    ln -s /usr/bin/clang++-18 /usr/local/bin/clang++ && \
    ln -s /usr/bin/ld.lld-18 /usr/local/bin/ld.lld && \
    printf '#!/bin/sh\ncase "$1" in --bindir) echo /usr/lib/llvm-18/bin;; --version) echo 18.1.3;; *) exit 1;; esac\n' > /usr/local/bin/llvm-config && \
    chmod +x /usr/local/bin/llvm-config

# Download and install precompiled PS5 Payload SDK v0.43
ENV PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
RUN mkdir -p /tmp/sdk && \
    curl -sL https://github.com/ps5-payload-dev/sdk/releases/download/v0.43/ps5-payload-sdk.zip -o /tmp/sdk.zip && \
    unzip -q /tmp/sdk.zip -d /tmp/sdk && \
    mv /tmp/sdk/ps5-payload-sdk ${PS5_PAYLOAD_SDK} && \
    rm -rf /tmp/sdk*

WORKDIR /work
CMD ["make", "ps5"]
