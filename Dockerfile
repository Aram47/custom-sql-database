# Multi-stage build for NoBugDB server image.
FROM ubuntu:24.04 AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN make build

FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /data

COPY --from=build /src/bin/nobugdb /usr/local/bin/nobugdb

EXPOSE 9000
VOLUME ["/data"]

CMD ["nobugdb", "--port", "9000", "--data-dir", "/data", "--log-level", "INFO"]
