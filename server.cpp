// cmd: gcc -o server server.cpp -lws2_32 -lwsock32 -L $MinGW\lib; .\server
#define _WIN32_WINNT 0x0600

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
// For Unix/Linux
// #include <arpa/inet.h> 
// #include <sys/socket.h>
// #include <netinet/ip.h>

#include <Winsock2.h>
#include <Ws2tcpip.h>

// Link with ws2_32.lib 
// for networking functionality in Windows
#pragma comment(lib, "Ws2_32.lib")

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
    // if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val)) == SOCKET_ERROR) {

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

    while (true) {
        // accept connection
        printf("-----\nSERVER RUNNING\n-----\n");
        struct sockaddr_in client_addr = {};
        // socklen_t socklen = sizeof(client_addr);
        int socklen = sizeof(client_addr);
        SOCKET connfd = accept(fd, (struct sockaddr*)&client_addr, &socklen);
        if (connfd == INVALID_SOCKET) {
            continue;
        }
        // int connfd = accept(fd, (struct sockaddr*)&client_addr, &socklen);
        // if (connfd < 0) {
        //     continue;
        // }
        // do something
        do_something(connfd);
        // close(connfd);
        // closesocket(connfd);
    }
    closesocket(fd);
    WSACleanup();

    return 0;
}