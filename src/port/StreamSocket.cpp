#include "StreamSocket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

StreamSocket::~StreamSocket() {
    DropClient();
    if (mListenFd >= 0) {
        close(mListenFd);
    }
}

bool StreamSocket::EnsureListening() {
    if (mListenFd >= 0) {
        return true;
    }

    mListenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (mListenFd < 0) {
        return false;
    }

    int one = 1;
    setsockopt(mListenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)mPort);

    if (bind(mListenFd, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(mListenFd, 1) != 0) {
        close(mListenFd);
        mListenFd = -1;
        return false;
    }

    fcntl(mListenFd, F_SETFL, O_NONBLOCK);
    return true;
}

void StreamSocket::PollAccept() {
    if (mClientFd >= 0 || !EnsureListening()) {
        return;
    }

    int fd = accept(mListenFd, nullptr, nullptr);
    if (fd < 0) {
        return;
    }

    // mListenFd is non-blocking (above) so this can be polled every
    // frame/tick without stalling the caller. On macOS/Darwin, accept()
    // inherits that O_NONBLOCK flag onto the new connection, so without this
    // the blocking Send() below fails on the first EWOULDBLOCK (whenever a
    // payload doesn't fit in the socket's send buffer in one syscall)
    // instead of actually blocking, dropping otherwise-healthy clients
    // almost immediately. Force the accepted socket back to blocking mode
    // explicitly so this can't happen regardless of platform inheritance
    // behavior.
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    mClientFd = fd;
}

void StreamSocket::DropClient() {
    if (mClientFd >= 0) {
        close(mClientFd);
        mClientFd = -1;
    }
}

bool StreamSocket::Send(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(mClientFd, data + sent, len - sent, 0);
        if (n <= 0) {
            DropClient();
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}
