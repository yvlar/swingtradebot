# ============================================================
#  Dockerfile  —  Environnement de build swing_bot
# ============================================================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ── Outils système + ICU pour curl ────────────────────────
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    libssl-dev \
    libcurl4-openssl-dev \
    sqlite3 \
    libsqlite3-dev \
    libicu-dev \
    && rm -rf /var/lib/apt/lists/*

# ── vcpkg ─────────────────────────────────────────────────
RUN git clone https://github.com/microsoft/vcpkg /vcpkg && \
    /vcpkg/bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_ROOT=/vcpkg
ENV PATH="$VCPKG_ROOT:$PATH"

# ── Dépendances C++ via vcpkg ─────────────────────────────
# curl est fourni par le système (libcurl4-openssl-dev)
# on skip curl dans vcpkg pour éviter le conflit ICU
RUN /vcpkg/vcpkg install \
    boost-beast \
    boost-asio \
    boost-system \
    nlohmann-json \
    sqlite3 \
    gtest

# ── Dossier de travail ────────────────────────────────────
WORKDIR /app
COPY . .

# ── CMakeLists.txt patché sans CMP0167 ───────────────────
# (la policy CMP0167 n'existe pas avant CMake 3.30)
RUN sed -i '/cmake_policy(SET CMP0167/d' CMakeLists.txt

# ── Build ─────────────────────────────────────────────────
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -G Ninja && \
    cmake --build build --target swing_bot -j$(nproc)

# ── Lancement du bot ──────────────────────────────────────
EXPOSE 9001
RUN cmake --build build --target unit_tests -j$(nproc)
RUN cmake --build build --target integration_tests -j$(nproc)
RUN cd build && ctest --output-on-failure
CMD ["./build/swing_bot"]