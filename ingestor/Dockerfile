# Dockerfile, Image, Container

FROM debian:bullseye 

WORKDIR /app
COPY . .

RUN apt-get update
RUN apt-get install -y gcc sqlite3 libsqlite3-dev python3 python3-pip libcurl4-openssl-dev gdb
RUN pip install netCDF4 numpy
RUN gcc -o ingestor main.c fetcher.c parser.c -lpthread -lsqlite3 -lcurl -ldl -lm
RUN chmod +x /app/entrypoint.sh

ENTRYPOINT [ "/app/entrypoint.sh" ]
CMD ["/app/ingestor"]

