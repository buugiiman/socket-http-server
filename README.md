# Simple HTTP-like Client/Server in C

A TCP client and server written in C with the Berkeley socket API. The client
sends stdin to the server in a small HTTP-style request and prints the
response; the server echoes the message back and handles multiple
concurrent clients.

## Protocol

Request:

    POST message SIMPLE/1.0
    Host: <server>
    Content-Length: <n>
    <blank line>
    <n bytes of body>

Response: `SIMPLE/1.0 200 OK` with the same body, or
`SIMPLE/1.0 400 Bad Request` for malformed requests. Lines end in CRLF.

## Features

- Binary-safe message handling (bodies can contain NUL bytes)
- Correct handling of partial `read()` / `write()` calls
- Input up to 10 MB, with a header size limit
- Header parsing: case-insensitive field names, flexible whitespace,
  fields in any order, <your validation rules>
- Concurrent connections via <prefork / select / epoll>: <1-2 sentences>
- Error handling: <list the cases you handle>

## Build and run

```bash
make
./s-server -p 1080
./s-client -p 1080 -s 127.0.0.1 < message.txt
```
