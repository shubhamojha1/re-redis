// cmd: 
// gcc -o server server.cpp -lws2_32 -lwsock32 -L $MinGW\lib; .\server
#define _WIN32_WINNT 0x0600

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <vector>
// For Unix/Linux
// #include <arpa/inet.h> 
// #include <sys/socket.h>
// #include <netinet/ip.h>

#include <Winsock2.h>
#include <Ws2tcpip.h>

// Link with ws2_32.lib 
// for networking functionality in Windows
#pragma comment(lib, "Ws2_32.lib")

const size_t K_MAX_MSG = 4096;

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2, // mark the connection for deletion
}

struct Conn {
    int fd = -1;
    uint32_t state = 0; // either STATE_REQ or STATE_RES
    // buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + K_MAX_MSG];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + K_MAX_MSG];
};

// For logging non-fatal messages
static void msg (const char *msg){
    fprintf(stderr, "%s\n", msg);
}

// To handle fatal errors
static void die(const char *msg){
    // int err = errno;
    int err = WSAGetLastError();
    fprintf(stderr, "[%d] %s\n", err, msg);
    WSACleanup(); // terminates use of the Winsock 2 DLL by cleaning resources
    abort();
}

// To handle client connection
static void do_something(int connfd){
    char rbuf[64] = {};
    // ssize_t n = read(connfd, rbuf, sizeof(rbuf)-1);
    // if (n<0) {
    //     msg("read() error");
    //     return;
    // }
    // printf("client says: %s\n", rbuf);

    // char wbuf[] = "world";
    // write(connfd, wbuf, strlen(wbuf));

    int n = recv(connfd, rbuf, sizeof(rbuf)-1, 0);
    if (n > 0) {
        rbuf[n] = '\0'; // Null-terminate the string
        printf("client says: %s\n", rbuf);

    const char wbuf[] = "world";
    send(connfd, wbuf, strlen(wbuf), 0);
    } else if (n == 0) {
        printf("The client closed the connection\n");
    } else {
        msg("recv() error");
    }
}

static int32_t read_full(SOCKET fd, char *buf, size_t n) { // (int fd)
    while (n > 0) {
        // ssize_t rv = read(fd, buf, n);
        int rv = recv(fd, buf, n, 0);
        // if (rv <= 0) {
        //     return -1; // error or unexpected EOF
        // }
        if (rv == 0) {
            return -2;
        } else if(rv < 0) {
            // An error occurred
            int err = WSAGetLastError();
            fprintf(stderr, "recv failed with error: %d\n", err);
            return -1;
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

// Request format
//  +-----+------+-----+------+-------
// | len | msg1 | len | msg2 | more...
//  +-----+------+-----+------+-------
static int32_t one_request(SOCKET connfd){ // (int connfd)
    // 4 bytes header
    char rbuf[4 + K_MAX_MSG + 1]; // size of one request
    // errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if (err == -2) {
        return -2;
        // msg(" --> read_full() error");
        // if (errno == 0) {
        //     msg("EOF");
        // } else {
        //     msg("read() error");
        // }
        // return err;
    } else if (err) {
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4); // assume little endian
    // printf("len--> %d\n", len);
    if (len > K_MAX_MSG) {
        msg("too long");
        return -1;
    }

    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }
    

    // request body
    rbuf[4 + len] = '\0';
    printf("client says: %s\n", &rbuf[4]);

    // reply using same protocol
    const char reply[] = "world";
    char wbuf[2 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);

    return write_all(connfd, wbuf, 4 + len);
}

int main(){

    WSADATA wsaData; // WSADATA structure 
                    // contains information about the Windows Sockets implementation.
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        die("WSAStartup failed");
    }

    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        die("socket()");
    }

    // // int fd = socket(AF_INET, SOCK_STREAM, 0); // from sys/socket.h
    // // passed to setsockopt()
    // if (fd < 0){
    //     die("socket()");
    // }

    int val = 1;
    // setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); // returns 0 on successful completion else -1

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val)) == SOCKET_ERROR) {
        die("setsockopt()");
    }

    // binding
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0); // wildcard address 0.0.0.0

    int rv = bind(fd, (const sockaddr*)&addr, sizeof(addr));
    if (rv) {
        die("bind()");
    }

    // listen for connections
    rv = listen(fd, SOMAXCONN);
    if (rv) {
        die("listen()");
    }

    // map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;

    // set the listen fd to nonblocking mode
    fd_set_nb(fd);

    // the event loop
    std::vector<struct pollfd> poll_args;

    while (true) {
        // accept connection
        printf("-----\nSERVER RUNNING\n-----\n");

        // struct sockaddr_in client_addr = {};
        // // socklen_t socklen = sizeof(client_addr);
        // int socklen = sizeof(client_addr);
        // SOCKET connfd = accept(fd, (struct sockaddr*)&client_addr, &socklen);
        // if (connfd == INVALID_SOCKET) {
        //     continue;
        // }

        // int connfd = accept(fd, (struct sockaddr*)&client_addr, &socklen);
        // if (connfd < 0) {
        //     continue;
        // }
        // do something

        // do_something(connfd);
    //     while (true) {
    //         // serves only one client connection at once
    //         int32_t err = one_request(connfd);
    //         // if (err) {
    //         //     break;
    //         // }

    //         if (err == -2) {
    //             msg("Client closed connection.");
    //             closesocket(connfd);
    //             break; 
    //         } else if (err) {
    //             closesocket(connfd);
    //             break;
    //         }
    //     }
    //     // close(connfd);
    //     closesocket(connfd); // for Windows
    // }
    // closesocket(fd);
    // WSACleanup();

    // ----- EVENT LOOP IMPLEMENTATION -----

    // prepare args of the poll()
        poll_args.clear()
        // listening fd put in first position, for convenience
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        
        // connection fds
        for (Conn *conn: fd2conn) {
            if(!conn) {
                continue;
            }
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        // poll for active fds
        // timeout arg not important
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if (rv < 0) {
            die("poll");
        }

        // process active connections
        for (size_t i = 1; i < poll_args.size(); i++) {
            if (poll_args[i].revents) {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END) {
                    // client closed normally, or something unexpected happened
                    // destroy connection
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }

        // try to accept a new connection if listening fd is active
        if (poll_args[0].revents) {
            (void)accept_new_conn(fd2conn, fd);

        }
    }

    return 0;
}