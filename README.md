# Simple HTTP-like Client/Server in C

A TCP client and server written in C with the Berkeley socket API. The client
sends stdin to the server in a small HTTP-style request and prints the
response; the server echoes the message back and handles multiple
concurrent clients.
