// aesdsocket.c
// Implementation for the assignment: TCP server on port 9000

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define PORT "9000"
#define BACKLOG 5
#define STORAGE_FILE "/var/tmp/aesdsocketdata"
#define RECV_BUF_SIZE 1024

static volatile sig_atomic_t exit_requested = 0;
static int listen_fd = -1;

static void signal_handler(int signum)
{
    (void)signum;
    syslog(LOG_INFO, "Caught signal, exiting");   // <-- ADDED
    exit_requested = 1;
}

static int setup_signal_handlers(void)
{
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) return -1;
    if (sigaction(SIGTERM, &sa, NULL) == -1) return -1;
    return 0;
}

static int create_and_bind(const char *port)
{
    struct addrinfo hints, *res, *rp;
    int sfd = -1, s;
    int opt = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    s = getaddrinfo(NULL, port, &hints, &res);
    if (s != 0) return -1;

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;

        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            close(sfd);
            continue;
        }

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(sfd);
        sfd = -1;
    }

    freeaddrinfo(res);
    return sfd;
}

static ssize_t safe_write_all(int fd, const void *buf, size_t count)
{
    const char *p = buf;
    size_t left = count;
    while (left > 0) {
        ssize_t written = write(fd, p, left);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        left -= (size_t)written;
        p += written;
    }
    return (ssize_t)count;
}

static int append_packet_to_file(const char *packet, size_t len)
{
    int fd = open(STORAGE_FILE, O_CREAT | O_RDWR | O_APPEND, 0644);
    if (fd == -1) return -1;

    if (safe_write_all(fd, packet, len) == -1) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int send_file_to_client(int client_fd)
{
    int fd = open(STORAGE_FILE, O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) return 0;
        return -1;
    }

    char buf[4096];
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        if (safe_write_all(client_fd, buf, (size_t)r) == -1) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return (r == -1) ? -1 : 0;
}

int main(int argc, char *argv[])
{
    bool daemon_mode = false;
    int opt;

    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
        case 'd':
            daemon_mode = true;
            break;
        default:
            fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    if (setup_signal_handlers() == -1) {
        syslog(LOG_ERR, "Failed to setup signal handlers: %s", strerror(errno));
        closelog();
        exit(EXIT_FAILURE);
    }

    listen_fd = create_and_bind(PORT);
    if (listen_fd == -1) {
        syslog(LOG_ERR, "Failed to create or bind socket: %s", strerror(errno));
        closelog();
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(listen_fd);
        closelog();
        exit(EXIT_FAILURE);
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "fork failed: %s", strerror(errno));
            close(listen_fd);
            closelog();
            exit(EXIT_FAILURE);
        }
        if (pid > 0) {
            close(listen_fd);
            closelog();
            exit(EXIT_SUCCESS);
        }
        if (setsid() == -1) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        }
        if (chdir("/") == -1) {}
        int fd = open("/dev/null", O_RDWR);
        if (fd != -1) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
    }

    while (!exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_fd == -1) {
            if (errno == EINTR) {
                if (exit_requested) break;   // <-- ADDED
                continue;
            }
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL) {
            strncpy(client_ip, "unknown", sizeof(client_ip));
            client_ip[sizeof(client_ip)-1] = '\0';
        }

        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        char recvbuf[RECV_BUF_SIZE];
        char *packet = NULL;
        size_t packet_len = 0;

        while (!exit_requested) {
            ssize_t r = recv(client_fd, recvbuf, sizeof(recvbuf), 0);
            if (r == 0) break;
            if (r < 0) {
                if (errno == EINTR) continue;
                syslog(LOG_ERR, "recv failed: %s", strerror(errno));
                break;
            }

            size_t off = 0;
            while (off < (size_t)r) {
                char *newline = memchr(recvbuf + off, '\n', (size_t)r - off);
                size_t chunk_len = (newline) ? (size_t)(newline - (recvbuf + off)) + 1 : (size_t)r - off;

                char *new_packet = realloc(packet, packet_len + chunk_len);
                if (!new_packet) {
                    syslog(LOG_ERR, "malloc/realloc failed");
                    free(packet);
                    packet = NULL;
                    packet_len = 0;
                    off += chunk_len;
                    if (newline) send_file_to_client(client_fd);
                    continue;
                }

                packet = new_packet;
                memcpy(packet + packet_len, recvbuf + off, chunk_len);
                packet_len += chunk_len;
                off += chunk_len;

                if (newline) {
                    if (append_packet_to_file(packet, packet_len) == -1) {
                        syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
                    } else {
                        if (send_file_to_client(client_fd) == -1) {
                            syslog(LOG_ERR, "Failed to send file to client: %s", strerror(errno));
                        }
                    }
                    free(packet);
                    packet = NULL;
                    packet_len = 0;
                }
            }
        }

        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);

        free(packet);
    }

    if (listen_fd != -1) close(listen_fd);
    unlink(STORAGE_FILE);
    closelog();
    return 0;
}