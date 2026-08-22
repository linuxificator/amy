#include "amy-service/src/main/cpp/amy_socket_transport.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int connect_client(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    assert(strlen(path) < sizeof(address.sun_path));
    strcpy(address.sun_path, path);

    for (int attempt = 0; attempt < 100; ++attempt) {
        if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0) return fd;
        if (errno != ENOENT && errno != ECONNREFUSED) break;
        usleep(1000);
    }

    perror("connect");
    assert(0 && "unable to connect to AMY socket");
    return -1;
}

static int wait_pop(amy_socket_server_t *server, char *buffer, size_t size) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        int result = amy_socket_server_pop(server, buffer, size);
        if (result != 0) return result;
        usleep(1000);
    }
    return 0;
}

int main(void) {
    char path[96];
    snprintf(path, sizeof(path), "/tmp/amy-socket-%ld.sock", (long)getpid());

    amy_socket_server_t *server = amy_socket_server_create();
    assert(server != NULL);
    assert(amy_socket_server_start(server, path) == 0);

    struct stat st;
    assert(stat(path, &st) == 0);
    assert((st.st_mode & 0777) == 0600);

    int client = connect_client(path);
    const char *batch = "n60l1i2Z\nv0F1200i2Z\n";
    assert(write(client, batch, strlen(batch)) == (ssize_t)strlen(batch));

    char command[AMY_SOCKET_MAX_MESSAGE];
    int length = wait_pop(server, command, sizeof(command));
    assert(length == (int)strlen("n60l1i2Z"));
    assert(strcmp(command, "n60l1i2Z") == 0);

    length = wait_pop(server, command, sizeof(command));
    assert(length == (int)strlen("v0F1200i2Z"));
    assert(strcmp(command, "v0F1200i2Z") == 0);

    /* Stream framing must survive a command split across separate writes. */
    assert(write(client, "K28", 3) == 3);
    usleep(1000);
    assert(amy_socket_server_pop(server, command, sizeof(command)) == 0);
    assert(write(client, "i2Z\r\n", 5) == 5);
    length = wait_pop(server, command, sizeof(command));
    assert(length == 6);
    assert(strcmp(command, "K28i2Z") == 0);

    /* Oversized lines are discarded as a unit and counted, not truncated. */
    char oversized[AMY_SOCKET_MAX_MESSAGE + 80];
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = '\n';
    uint32_t dropped_before = amy_socket_server_dropped(server);
    assert(write(client, oversized, sizeof(oversized)) == (ssize_t)sizeof(oversized));
    for (int attempt = 0; attempt < 500 && amy_socket_server_dropped(server) == dropped_before; ++attempt) {
        usleep(1000);
    }
    assert(amy_socket_server_dropped(server) == dropped_before + 1u);
    assert(amy_socket_server_pop(server, command, sizeof(command)) == 0);

    close(client);
    amy_socket_server_stop(server);
    assert(access(path, F_OK) != 0);
    amy_socket_server_destroy(server);

    printf("android socket transport: ok\n");
    return 0;
}
