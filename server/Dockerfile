# Dockerfile, Image, Container

FROM gcc

WORKDIR /app
COPY . .

RUN apt-get update
RUN apt-get install -y libsqlite3-dev
RUN gcc -o server main.c mongoose.c -lpthread -lsqlite3 -ldl -lm

CMD ["./server"]
