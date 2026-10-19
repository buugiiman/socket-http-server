#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "macro.h"

#define BACKLOG 10

static int read_header(int client_fd, char *buf);
static int parse_header(char *buf, long *content_length);
static int read_body(int client_fd, long content_len, char *body);
static void bad_request(int client_fd);
static void success_request(int client_fd, long content_len, const char *msg);
static void handle_request(int sock);
static char *trim(char *str);
static ssize_t send_all(int fd, const void *buf, size_t len);
static void debug_log(const char *run_id, const char *hypothesis_id, const char *location, const char *message, const char *data_json);

static ssize_t send_all(int fd, const void *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, (const char *)buf + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }

    return (ssize_t)sent;
}

static void handle_request(int sock) {
    char header_buf[MAX_HDR + 1];
    long content_length = 0;
    char *body_buf = NULL;

    if (read_header(sock, header_buf) < 0) {
        bad_request(sock);
        return;
    }

    if (parse_header(header_buf, &content_length) < 0) {
        //drain_socket(sock);
        bad_request(sock);
        return;
    }

    body_buf = (char *)malloc((size_t)content_length + 1);
    if (body_buf == NULL) {
        //drain_socket(sock);
        bad_request(sock);
        return;
    }

    if (read_body(sock, content_length, body_buf) < 0) {
        //drain_socket(sock);
        bad_request(sock);
        free(body_buf);
        return;
    }
    body_buf[content_length] = '\0';

    success_request(sock, content_length, body_buf);
    free(body_buf);

    return;
}

static int read_header(int client_fd, char *buf) {
    int i = 0;

    while (i < MAX_HDR) {
        ssize_t r = read(client_fd, &buf[i], 1);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return -1;
        }

        i++;
        if (i >= 4 && buf[i - 4] == '\r' && buf[i - 3] == '\n' && buf[i - 2] == '\r' && buf[i - 1] == '\n') {
            buf[i] = '\0';
            return i;
        }
    }

    return -1;
}

static char *trim(char *str) {
    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return str;
}

static int parse_header(char *buf, long *content_length) {
    char tmp[MAX_HDR + 1];
    int has_host_line = 0;
    int has_length_line = 0;

    memcpy(tmp, buf, MAX_HDR);
    tmp[MAX_HDR] = '\0';

    char *line = strtok(tmp, "\r\n");
    if (line == NULL) {
        return -1;
    }

    char method[16];
    char target[16];
    char version[16];
    if (sscanf(line, "%15s %15s %15s", method, target, version) != 3) {
        return -1;
    }
    if (strcmp(method, "POST") != 0 || strcmp(target, "message") != 0 || strcmp(version, "SIMPLE/1.0") != 0) {
        return -1;
    }

    while ((line = strtok(NULL, "\r\n")) != NULL) {
        char *colon = strchr(line, ':');
        if (colon == NULL) {
            continue;
        }

        *colon = '\0';
        char *name = trim(line);
        char *value = trim(colon + 1);

        if (strcasecmp(name, "Host") == 0) {
            if (*value == '\0') {
                return -1;
            }
            has_host_line = 1;
        } else if (strcasecmp(name, "Content-Length") == 0) {
            char *endptr = NULL;
            long parsed = strtol(value, &endptr, 10);
            if (*endptr != '\0' || parsed < 0 || parsed > MAX_CONT) {
                return -1;
            }
            *content_length = parsed;
            has_length_line = 1;
        }
    }

    if (!has_host_line || !has_length_line) {
        return -1;
    }

    return 0;
}

static int read_body(int client_fd, long content_len, char *body) {
    long i = 0;
    while (i < content_len) {
        ssize_t read_bytes = read(client_fd, body + i, (size_t)(content_len - i));
        if (read_bytes < 0) {
            return -1;
        }
        if (read_bytes == 0) {
            return -1;
        }
        i += read_bytes;
    }
    return 0;
}

static void bad_request(int client_fd) {
    char *bad_req = "SIMPLE/1.0 400 Bad Request\r\n\r\n";
    ssize_t sent = send_all(client_fd, bad_req, strlen(bad_req));
}

static void success_request(int client_fd, long content_len, const char *msg) {
    char response_header[1024];
    int header_len = snprintf(
        response_header,
        sizeof(response_header),
        "SIMPLE/1.0 200 OK\r\nContent-Length: %ld\r\n\r\n",
        content_len);
    if (header_len < 0 || header_len >= (int)sizeof(response_header)) {
        return;
    }

    if (send_all(client_fd, response_header, (size_t)header_len) < 0) {
        return;
    }
    (void)send_all(client_fd, msg, (size_t)content_len);
}

int main(const int argc, const char **argv) {
    int i;
    int port = -1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && (i + 1) < argc) {
            port = atoi(argv[i + 1]);
            i++;
        }
    }
    if (port < 1024 || port > 65535) {
        printf("usage: %s -p port\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int yes = 1;
    int rv;
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    char port_str[6];

    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, port_str, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            close(sockfd);
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo);

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return 1;
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        close(sockfd);
        return 1;
    }

    while (1) {
        int clientfd = accept(sockfd, NULL, NULL);
        if (clientfd < 0) {
            continue;
        }
        handle_request(clientfd);
        close(clientfd);
        break;
    }
}
