#ifndef AMY_SOCKET_TRANSPORT_H
#define AMY_SOCKET_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMY_SOCKET_MAX_MESSAGE 1024u
#define AMY_SOCKET_QUEUE_CAPACITY 256u

typedef struct amy_socket_server amy_socket_server_t;

/** Allocate a stopped server. Allocation is startup-only, never realtime. */
amy_socket_server_t *amy_socket_server_create(void);

/**
 * Bind a pathname AF_UNIX/SOCK_STREAM socket, chmod it to 0600, listen, and
 * start the receive thread. Messages are newline-delimited byte strings.
 * Returns 0 or a negative errno/pthread error value.
 */
int amy_socket_server_start(amy_socket_server_t *server, const char *path);

/** Stop the receive thread, close descriptors, and unlink the socket path. */
void amy_socket_server_stop(amy_socket_server_t *server);

/** Stop and free a server created by amy_socket_server_create(). */
void amy_socket_server_destroy(amy_socket_server_t *server);

/**
 * Realtime-safe single-consumer pop. Returns message length, 0 when empty,
 * or a negative error. No allocation, file I/O, socket I/O, or locks occur.
 */
int amy_socket_server_pop(
    amy_socket_server_t *server,
    char *destination,
    size_t destination_size);

/** Number of complete messages dropped because the ring was full/oversized. */
uint32_t amy_socket_server_dropped(const amy_socket_server_t *server);

#ifdef __cplusplus
}
#endif

#endif
