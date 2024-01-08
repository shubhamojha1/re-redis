// cmd: 
// gcc -o client client.cpp -lws2_32 -lwsock32 -L $MinGW\lib; .\client
#define _WIN32_WINNT 0x0600

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
// For Unix/Linux
// #include <arpa/inet.h> 
// #include <sys/socket.h>
// #include <netinet/ip.h>

#include <Winsock2.h>
#include <Ws2tcpip.h>

// Link with ws2_32.lib 
// for networking functionality in Windows
#pragma comment(lib, "Ws2_32.lib")

// static void die(const char *msg){
//     int err = errno;
//     fprintf(stderr, "[%d] %s\n", err, msg);
//     abort();

// }

static void die(const char *msg){
    // int err = errno;
    int err = WSAGetLastError();
    fprintf(stderr, "[%d] %s\n", err, msg);
    WSACleanup(); // terminates use of the Winsock 2 DLL
    abort();
}

static int32_t read_full(SOCKET fd, char *buf, size_t n) { // (int fd)
    while (n > 0) {
        // ssize_t rv = read(fd, buf, n);
        int rv = recv(fd, buf, n, 0);
        if (rv <= 0) {
            return -1; // error or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(SOCKET fd, const char *buf, size_t n) { // (int fd)
    while (n > 0){
        // ssize_t rv = write(fd, buf, n);
        int rv = send(fd, buf, n, 0);
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t query(int fd, const char *text) {
    uint32_t len = (uint32_t)strlen(text);
    if (len > K_MAX_MSG) {
        return -1;
    }

    char wbuf[4 + K_MAX_MSG];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
    if (int32_t err = write_all(fd, wbuf, 4 + len)) {
        return err;
    }

    // 4 bytes header
    char rbuf[4 + K_MAX_MSG + 1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (!errno) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }
    memcpy(&len, rbuf, 4); // assuming little endian
    if (len > K_MAX_MSG) {
        msg("too long");
        return -1;
    }
    // reply body
}

int main() {
    // int fd = socket(AF_INET, SOCK_STREAM, 0);
    // if (fd < 0){
    //     die("socket()");
    // }

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        die("WSAStartup failed");
    }
    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        die("socket()");
    } 

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    // addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // 127.0.0.1
    // char ip[] = "127.0.0.1";
    // inetPton(AF_INET, ip , &addr.sin_addr);

    char ip[] = "127.0.0.1";
    int ipSize = sizeof(addr);
    SOCKADDR_IN sockAddr;
    if (WSAStringToAddress(ip, AF_INET, NULL, (LPSOCKADDR)&sockAddr, &ipSize) != 0) {
        die("WSAStringToAddress failed");
    }
    addr.sin_addr.s_addr = sockAddr.sin_addr.s_addr;

    // if (WSAStringToAddress(ip, AF_INET, NULL, (struct sockaddr*)&addr.sin_addr, &ipSize) != 0) {
    //     die("WSAStringToAddress Failed");
    // }

    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv == SOCKET_ERROR) {
        die("connect");
    }
    // if (rv) {
    //     die("connect");
    // }

    // #-> For single request 
    // char msg[] = "hello";
    // // write(fd, msg, strlen(msg));
    // send(fd, msg, strlen(msg), 0);

    // char rbuf[64] = {};
    // #

    // Multiple requests
    int32_t err = query(fd, "hello1");
    if (err) {
        goto L_DONE;
    }
    err = query(fd, "hello2");
    if (err) {
        goto L_DONE;
    }
    err = query(fd, "hello3");
    if (err) {
        goto L_DONE;
    }

    L_DONE:
        close(fd);
        return 0;


    // ssize_t n = read(fd, rbuf, sizeof(rbuf)-1);
    // if (n == SOCKET_ERROR) {
    //     die("read");
    // }

    // if (n < 0) {
    //     die("read");
    // }
    int n = recv(fd, rbuf, sizeof(rbuf), 0);
    if (n > 0) {
        rbuf[n] = '\0';
    }
    else if (n == 0)
        printf("The server closed the connection\n");
    else
        die("recv");
    printf("server says: %s\n", rbuf);
    closesocket(fd);
    WSACleanup();
    // close(fd);
    return 0;
}