FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev libssl-dev gcc-mingw-w64-i686 make && rm -rf /var/lib/apt/lists/*
WORKDIR /src
ENTRYPOINT ["make"]
