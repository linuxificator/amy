#include <jni.h>

#include <cerrno>
#include <cstddef>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

int connect_socket(const char *path) {
    if (path == nullptr || path[0] == '\0') return -EINVAL;

    sockaddr_un addr{};
    const size_t path_len = std::strlen(path);
    if (path_len >= sizeof(addr.sun_path)) return -ENAMETOOLONG;

    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path, path_len + 1u);

    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;

    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        const int saved = errno;
        close(fd);
        return -saved;
    }

    // Musical control must never stall a UI/framework thread behind a full
    // socket buffer. After the connection is established, make sends
    // non-blocking; callers can detect -EAGAIN and decide how to recover.
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        const int saved = errno;
        close(fd);
        return -saved;
    }

    return fd;
}

int send_wire(int fd, JNIEnv *env, jstring wire) {
    if (fd < 0) return -ENOTCONN;
    if (wire == nullptr) return -EINVAL;

    const jsize len = env->GetStringUTFLength(wire);
    if (len <= 0) return -EINVAL;

    const char *bytes = env->GetStringUTFChars(wire, nullptr);
    if (bytes == nullptr) return -ENOMEM;

    const ssize_t sent = send(fd,
                              bytes,
                              static_cast<size_t>(len),
                              MSG_NOSIGNAL | MSG_DONTWAIT);
    const int saved = sent < 0 ? errno : 0;
    env->ReleaseStringUTFChars(wire, bytes);

    if (sent < 0) return -saved;
    if (sent != len) return -EIO;
    return 0;
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_org_amy_audio_AmyClient_nativeConnect(JNIEnv *env, jclass, jstring socketPath) {
    if (socketPath == nullptr) return -EINVAL;
    const char *path = env->GetStringUTFChars(socketPath, nullptr);
    if (path == nullptr) return -ENOMEM;
    const int result = connect_socket(path);
    env->ReleaseStringUTFChars(socketPath, path);
    return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_amy_audio_AmyClient_nativeSend(JNIEnv *env, jclass, jint fd, jstring wire) {
    return send_wire(fd, env, wire);
}

extern "C" JNIEXPORT void JNICALL
Java_org_amy_audio_AmyClient_nativeClose(JNIEnv *, jclass, jint fd) {
    if (fd >= 0) close(fd);
}
