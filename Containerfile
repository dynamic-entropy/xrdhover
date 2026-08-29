# Build linux/amd64 xrdhover against AlmaLinux 10 (el10) + XRootD client.
# Usage: see scripts/build-release.sh
ARG ALMA_VERSION=10
FROM --platform=linux/amd64 almalinux:${ALMA_VERSION}

RUN dnf -y install epel-release \
 && dnf -y install 'dnf-command(config-manager)' \
 && dnf config-manager --set-enabled crb \
 && dnf config-manager --add-repo https://xrootd.web.cern.ch/xrootd.repo \
 && dnf -y install \
      cmake \
      gcc-c++ \
      make \
      git \
      openssl-devel \
      libcurl-devel \
      xrootd-client-devel \
 && dnf clean all

WORKDIR /src
COPY . /src

# Set by scripts/build-release.sh from the release tag so XRDHOVER_VERSION
# matches the tarball / GitHub release (not a stale CMakeLists default).
ARG VERSION=
RUN cmake_args="-DCMAKE_BUILD_TYPE=Release -DXRDHOVER_ENABLE_TESTS=OFF" \
 && if [ -n "$VERSION" ]; then cmake_args="$cmake_args -DXRDHOVER_VERSION=$VERSION"; fi \
 && cmake -S . -B /build $cmake_args \
 && cmake --build /build -j"$(nproc)" --target xrdhover \
 && mkdir -p /out \
 && cp /build/xrdhover /out/xrdhover \
 && /out/xrdhover version \
 && if [ -n "$VERSION" ]; then \
      reported="$(/out/xrdhover version | awk '{print $2}')"; \
      if [ "$reported" != "$VERSION" ]; then \
        echo "error: binary version ${reported} != release VERSION ${VERSION}" >&2; \
        exit 1; \
      fi; \
    fi

CMD ["true"]
