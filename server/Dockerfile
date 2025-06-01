# Dockerfile, Image, Container

FROM gcc

WORKDIR /app
COPY . .

RUN apt-get update && \
    apt-get install -y libsqlite3-dev && \
    gcc -o server main.c -lpthread -lsqlite3 -ldl -lm

CMD ["./server"]
