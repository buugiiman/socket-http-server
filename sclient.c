#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>

#include "macro.h"
/*--------------------------------------------------------------------------------*/

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

ssize_t send_to_server(int socket, const void *buf, size_t len) {
    size_t sent = 0;
    ssize_t n;

    while (sent < len) {
        n = write(socket, (char*) buf + sent, len - sent);
        if (n <= 0) {
            return -1;
        }
        sent += n;
    }

    return sent;
}

int read_line(int socket, char *buf, int len) {
   int i = 0;
   char c;
   ssize_t n;

    while (i < len - 1) {
        n = read(socket, &c, 1);

        if(n == 1) {
            buf[i++] = c;

            if(c == '\n') {
                break;
            }
        } else if(n == 0) {
            break;
        } else {
            return -1;
        }
    }

    buf[i] = '\0';
    return i;
}

int 
main(const int argc, const char** argv) 
{
    const char *pserver = NULL;
    int port = -1;
    int i;
      
    /* argument processing */
    for (i = 1; i < argc; i++)  {
        if (strcmp(argv[i], "-p") == 0 && (i + 1) < argc) {
            port = atoi(argv[i+1]);
            i++;
        } else if (strcmp(argv[i], "-s") == 0 && (i + 1) < argc) {
            pserver = argv[i+1];
            i++;
        }
    }

    /* check arguments */
    if (port < 0 || pserver == NULL) {
        printf("usage: %s -p port -s server-ip\n", argv[0]);
        exit(-1);
    }
    if (port < 1024 || port > 65535) {
        printf("port number should be between 1024 ~ 65535.\n");
        exit(-1);
    }

    // implement your own code

    int client_fd;
    size_t numbytes = 0;
    size_t n;
    
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    char header_buf[MAX_HDR];
    char *message_buf = (char *)malloc(MAX_CONT);
    char port_to_str[6];

    if (message_buf == NULL) {
        fprintf(stderr, "failed to allocate message buffer\n");
        return 1;
    }

    snprintf(port_to_str, sizeof(port_to_str), "%d", port);

    memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(pserver, port_to_str, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        free(message_buf);
        return 1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
	    if ((client_fd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

        inet_ntop(p->ai_family,
            get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
        
		if (connect(client_fd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			close(client_fd);
			continue;
		}
		break;
	}

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        free(message_buf);
        return 2;
    }

    inet_ntop(p->ai_family,
			get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
            
	freeaddrinfo(servinfo); // all done with this structure
    
    char temp[4096];

    while ((n = fread(temp, 1, sizeof(temp), stdin)) > 0) {
        size_t to_copy = n;

        if(numbytes + n > MAX_CONT) {
            to_copy = MAX_CONT - numbytes;
        }

        if (to_copy > 0) {
            memcpy(message_buf + numbytes, temp, to_copy);
            numbytes += to_copy;
        }

        if (numbytes >= MAX_CONT) {
            break;
        }
    }

    if (numbytes == 0) {
        fprintf(stderr, "empty message\n");
    }

    snprintf(header_buf, sizeof(header_buf), "POST message SIMPLE/1.0\r\nHost: %s\r\nContent-Length: %zd\r\n\r\n", pserver, numbytes);

    if (send_to_server(client_fd, header_buf, strlen(header_buf)) < 0) {
        fprintf(stderr, "failed to send the message to the server\n");
        close(client_fd);
        free(message_buf);

        return 1;
    }

    if (send_to_server(client_fd, message_buf, numbytes) < 0) {
        fprintf(stderr, "failed to send the message to the server\n");
        close(client_fd);
        free(message_buf);

        return 1;
    }

    char lines[1024];

    //int first_line_n = read_line(client_fd, lines, sizeof(lines));
    int first_line_n = read_line(client_fd, lines, sizeof(lines));
    if (first_line_n <= 0) {
        printf("connection closed by server\n");
        close(client_fd);
        free(message_buf);
        return 0;
    }



    int is_ok = (strstr(lines, "200 OK") != NULL);
    if (!is_ok) {
        printf("%s", lines);
    }

    while (1) {
        int n = read_line(client_fd, lines, sizeof(lines));
        if (n <= 0) {
            break;
        }
        if (!is_ok) {
            printf("%s", lines);
            if (strcmp(lines, "\r\n") == 0) {
                break;
            }
        } else if (strcmp(lines, "\r\n") == 0) {
            break;
        }
    }

    if (is_ok) {
        char *buffer = (char *)malloc(MAX_CONT);
        ssize_t n;
        if (buffer == NULL) {
            close(client_fd);
            free(message_buf);
            fprintf(stderr, "failed to allocate response buffer\n");
            return 1;
        }

        while ((n = read(client_fd, buffer, MAX_CONT)) > 0) {
            fwrite(buffer, 1, n, stdout);
        }
        free(buffer);
    }

    close(client_fd);
    free(message_buf);
}