#include "amy_socket_transport.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

struct amy_socket_entry {
    uint16_t length;
    char message[AMY_SOCKET_MAX_MESSAGE];
};

struct amy_socket_server {
    pthread_t thread;
    bool thread_started;
    atomic_bool running;
    atomic_int listen_fd;
    atomic_int client_fd;
    atomic_uint write_index;
    atomic_uint read_index;
    atomic_uint dropped;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct amy_socket_entry queue[AMY_SOCKET_QUEUE_CAPACITY];
};

static void close_socket_fd(atomic_int *slot) {
    int fd = atomic_exchange_explicit(slot, -1, memory_order_acq_rel);
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

static bool queue_push(amy_socket_server_t *server, const char *message, size_t length) {
    if (length == 0 || length >= AMY_SOCKET_MAX_MESSAGE) {
        atomic_fetch_add_explicit(&server->dropped, 1u, memory_order_relaxed);
        return false;
    }

    uint32_t write = atomic_load_explicit(&server->write_index, memory_order_relaxed);
    uint32_t read = atomic_load_explicit(&server->read_index, memory_order_acquire);
    if ((uint32_t)(write - read) >= AMY_SOCKET_QUEUE_CAPACITY) {
        atomic_fetch_add_explicit(&server->dropped, 1u, memory_order_relaxed);
        return false;
    }

    struct amy_socket_entry *entry = &server->queue[write % AMY_SOCKET_QUEUE_CAPACITY];
    memcpy(entry->message, message, length);
    entry->message[length] = '\0';
    entry->length = (uint16_t)length;
    atomic_store_explicit(&server->write_index, write + 1u, memory_order_release);
    return true;
}

static void release_client_fd(amy_socket_server_t *server, int fd) {
    int expected = fd;
    if (atomic_compare_exchange_strong_explicit(
            &server->client_fd,
            &expected,
            -1,
            memory_order_acq_rel,
            memory_order_acquire)) {
        close(fd);
    }
}

static void *socket_thread_main(void *context) {
    amy_socket_server_t *server = (amy_socket_server_t *)context;

    while (atomic_load_explicit(&server->running, memory_order_acquire)) {
        int listen_fd = atomic_load_explicit(&server->listen_fd, memory_order_acquire);
        if (listen_fd < 0) break;

        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (!atomic_load_explicit(&server->running, memory_order_acquire)) break;
            continue;
        }

        atomic_store_explicit(&server->client_fd, fd, memory_order_release);

        char message[AMY_SOCKET_MAX_MESSAGE];
        size_t message_length = 0;
        bool overflow = false;
        char input[512];

        while (atomic_load_explicit(&server->running, memory_order_acquire)) {
            ssize_t received = read(fd, input, sizeof(input));
            if (received == 0) break;
            if (received < 0) {
                if (errno == EINTR) continue;
                break;
            }

            for (ssize_t i = 0; i < received; ++i) {
                unsigned char ch = (unsigned char)input[i];
                if (ch == '\n') {
                    if (overflow) {
                        atomic_fetch_add_explicit(&server->dropped, 1u, memory_order_relaxed);
                    } else if (message_length > 0) {
                        queue_push(server, message, message_length);
                    }
                    message_length = 0;
                    overflow = false;
                    continue;
                }
                if (ch == '\r') continue;

                if (overflow) continue;
                if (message_length + 1u >= sizeof(message)) {
                    overflow = true;
                    continue;
                }
                message[message_length++] = (char)ch;
            }
        }

        release_client_fd(server, fd);
    }

    return NULL;
}

amy_socket_server_t *amy_socket_server_create(void) {
    amy_socket_server_t *server = (amy_socket_server_t *)calloc(1, sizeof(*server));
    if (server == NULL) return NULL;

    atomic_init(&server->running, false);
    atomic_init(&server->listen_fd, -1);
    atomic_init(&server->client_fd, -1);
    atomic_init(&server->write_index, 0u);
    atomic_init(&server->read_index, 0u);
    atomic_init(&server->dropped, 0u);
    return server;
}

int amy_socket_server_start(amy_socket_server_t *server, const char *path) {
    if (server == NULL || path == NULL || path[0] == '\0') return -EINVAL;
    if (server->thread_started || atomic_load_explicit(&server->running, memory_order_acquire)) {
        return -EALREADY;
    }

    size_t path_length = strlen(path);
    if (path_length >= sizeof(server->path)) return -ENAMETOOLONG;

    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, path_length + 1u);

    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -errno;

    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        int error = errno;
        close(fd);
        return -error;
    }

    if (chmod(path, S_IRUSR | S_IWUSR) != 0) {
        int error = errno;
        close(fd);
        unlink(path);
        return -error;
    }

    if (listen(fd, 1) != 0) {
        int error = errno;
        close(fd);
        unlink(path);
        return -error;
    }

    memcpy(server->path, path, path_length + 1u);
    atomic_store_explicit(&server->write_index, 0u, memory_order_relaxed);
    atomic_store_explicit(&server->read_index, 0u, memory_order_relaxed);
    atomic_store_explicit(&server->dropped, 0u, memory_order_relaxed);
    atomic_store_explicit(&server->listen_fd, fd, memory_order_release);
    atomic_store_explicit(&server->running, true, memory_order_release);

    int result = pthread_create(&server->thread, NULL, socket_thread_main, server);
    if (result != 0) {
        atomic_store_explicit(&server->running, false, memory_order_release);
        close_socket_fd(&server->listen_fd);
        unlink(server->path);
        server->path[0] = '\0';
        return -result;
    }

    server->thread_started = true;
    return 0;
}

void amy_socket_server_stop(amy_socket_server_t *server) {
    if (server == NULL) return;

    atomic_store_explicit(&server->running, false, memory_order_release);
    close_socket_fd(&server->client_fd);
    close_socket_fd(&server->listen_fd);

    if (server->thread_started) {
        pthread_join(server->thread, NULL);
        server->thread_started = false;
    }

    if (server->path[0] != '\0') {
        unlink(server->path);
        server->path[0] = '\0';
    }
}

void amy_socket_server_destroy(amy_socket_server_t *server) {
    if (server == NULL) return;
    amy_socket_server_stop(server);
    free(server);
}

int amy_socket_server_pop(
    amy_socket_server_t *server,
    char *destination,
    size_t destination_size) {
    if (server == NULL || destination == NULL || destination_size == 0) return -EINVAL;

    uint32_t read_index = atomic_load_explicit(&server->read_index, memory_order_relaxed);
    uint32_t write_index = atomic_load_explicit(&server->write_index, memory_order_acquire);
    if (read_index == write_index) return 0;

    const struct amy_socket_entry *entry =
        &server->queue[read_index % AMY_SOCKET_QUEUE_CAPACITY];
    size_t length = entry->length;
    if (length + 1u > destination_size) {
        atomic_store_explicit(&server->read_index, read_index + 1u, memory_order_release);
        return -EMSGSIZE;
    }

    memcpy(destination, entry->message, length + 1u);
    atomic_store_explicit(&server->read_index, read_index + 1u, memory_order_release);
    return (int)length;
}

uint32_t amy_socket_server_dropped(const amy_socket_server_t *server) {
    if (server == NULL) return 0;
    return atomic_load_explicit(&server->dropped, memory_order_relaxed);
}
