// cmd: 
// gcc -o client client.cpp -lws2_32 -lwsock32 -L $MinGW\lib; .\client
// g++ -I%BOOST_ROOT% -L%BOOST_ROOT%\stage\lib -o client client.cpp -lwsock32 -lws2_32; ./client

#define _WIN32_WINNT _WIN32_WINNT_WIN10 // 0x0600

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <string>
#include <vector>
#include <iostream>
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
static void msg (const char *msg){
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg){
    // int err = errno;
    int err = WSAGetLastError();
    fprintf(stderr, "[%d] %s\n", err, msg);
    WSACleanup(); // terminates use of the Winsock 2 DLL
    abort();
}

static int32_t read_full(SOCKET fd, char *buf, size_t n) { // (int fd)

    printf("inside read_full()\n");
    while (n > 0) {
        // ssize_t rv = read(fd, buf, n);
        // int rv = recv(fd, buf, n, 0);

        printf("Attempting to receive from server...\n", n);
        int rv = recv(fd, buf, n, 0);
        if (rv < 0) {
            printf("send() failed with error: %d\n", WSAGetLastError());
        } else {
            printf("Successfully sent %d bytes to server.\n", rv);
        }

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

    printf("inside write_all()\n");
    // while (*buf != '\0') {
    //     printf("%c", *buf);
    //     ++buf;
    // }

    while (n > 0){
        // ssize_t rv = write(fd, buf, n);
        // int rv = send(fd, buf, n, 0);
        // printf("Sending to server: %.*s", *buf);
        // std::cout << buf << "\n";

        printf("Attempting to send %zu bytes to server...\n", n);
        int rv = send(fd, buf, n, 0);
        if (rv < 0) {
            printf("send() failed with error: %d\n", WSAGetLastError());
        } else {
            printf("Successfully sent %d bytes to server.\n", rv);
        }

        if (rv <= 0) {
            return -1; // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;

        printf("size of n: %d\n", n);
        printf("size of rv: %d\n", rv);
        printf("size of buf (pointer): %d", buf);
    }
    return 0;
}

const size_t K_MAX_MSG = 4096;

// static int32_t query(SOCKET fd, const char *text) {
//     uint32_t len = (uint32_t)strlen(text);
//     if (len > K_MAX_MSG) {
//         return -1;
//     }

//     char wbuf[4 + K_MAX_MSG];
//     memcpy(wbuf, &len, 4);
//     memcpy(&wbuf[4], text, len);
//     if (int32_t err = write_all(fd, wbuf, 4 + len)) {
//         return err;
//     }

//     // 4 bytes header
//     char rbuf[4 + K_MAX_MSG + 1];
//     // errno = 0; use WSAGetLastError() instead
//     int32_t err = read_full(fd, rbuf, 4);
//     if (err) {
//         fprintf(stderr, "read_full() error: %d\n", WSAGetLastError());
//         return err;
//         // if (!errno) {
//         //     msg("EOF");
//         // } else {
//         //     msg("read() error");
//         // }
//         // return err;
//     }
//     memcpy(&len, rbuf, 4); // assuming little endian
//     if (len > K_MAX_MSG) {
//         msg("Received message too long");
//         return -1;
//     }
//     // reply body
//     err = read_full(fd, &rbuf[4], len);
//     if (err) {
//         // msg("read() error");
//         fprintf(stderr, "(reply) read_full() error: %d\n", WSAGetLastError());
//         return err;
//     }

//     // do something equivalent
//     rbuf[4 + len] = '\0';
//     printf("server says: %s\n", &rbuf[4]);
//     return 0;
// }

// query() changed to -> send_req() and read_res()
// static int32_t send_req(int fd, const char *text) {
//     uint32_t len = (uint32_t)strlen(text);
//     if (len > K_MAX_MSG) {
//         return -1;
//     }

//     char wbuf[4 + K_MAX_MSG];
//     memcpy(wbuf, &len, 4);  // assume little endian
//     memcpy(&wbuf[4], text, len);
//     return write_all(fd, wbuf, 4 + len);
// }

static int32_t send_req(SOCKET fd, const std::vector<std::string> &cmd) {

    printf("inside send_req()\n");
    uint32_t len = 4;
    for (const std::string &s : cmd) {
        len += 4 + s.size(); // adding 4, to accomodate for header
    }

    if(len > K_MAX_MSG) // if greater than MAX ALLOWED
        return -1;

    char wbuf[4 + K_MAX_MSG];
    memcpy(&wbuf[0], &len, 4);
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);
    size_t cur = 8;

    for(const std::string &s: cmd){ // filling up wbuf
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4);
        memcpy(&wbuf[cur + 4], s.data(), s.size());
        cur += 4 + s.size();
    }

    return write_all(fd, wbuf, 4 + len);
}

static int32_t read_res(SOCKET fd) {
    // 4 bytes header
    char rbuf[4 + K_MAX_MSG + 1];
    printf("inside read_res\n");
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }
    printf("read_full works fine\n");

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > K_MAX_MSG) {
        msg("too long");
        return -1;
    }

    // reply body
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // // do something
    // rbuf[4 + len] = '\0';
    // printf("server says: %s\n", &rbuf[4]);
    // return 0;
    
    // print result
    uint32_t rescode = 0;
    if (len < 4) {
        msg("bad response");
        return -1;
    }
    memcpy(&rescode, &rbuf[4], 4);
    printf("just before server says\n");
    printf("server says: [%u] %.*s\n", rescode, len - 4, &rbuf[8]);
    return 0;
}

int main(int argc, char **argv) {
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
    // int32_t err = query(fd, "hello1");
    // if (err) {
    //     goto L_DONE;
    // }
    // err = query(fd, "hello2");
    // if (err) {
    //     goto L_DONE;
    // }
    // err = query(fd, "hello3");
    // if (err) {
    //     goto L_DONE;
    // }

    // L_DONE:
    //     // close(fd);
    //     closesocket(fd);
    //     return 0;


    // ssize_t n = read(fd, rbuf, sizeof(rbuf)-1);
    // if (n == SOCKET_ERROR) {
    //     die("read");
    // }

    // if (n < 0) {
    //     die("read");
    // }
    // int n = recv(fd, rbuf, sizeof(rbuf), 0);
    // if (n > 0) {
    //     rbuf[n] = '\0';
    // }
    // else if (n == 0)
    //     printf("The server closed the connection\n");
    // else
    //     die("recv");
    // printf("server says: %s\n", rbuf);
    // closesocket(fd);
    // WSACleanup();
    // // close(fd);
    // return 0;

    // multiple pipelined requests
    // const char *query_list[3] = {"hello1", "hello2", "hello3"};
    // for(ssize_t i = 0; i < 3; i++) {
    //     int32_t err = send_req(fd, query_list[i]);
    //     if (err) { 
    //         goto L_DONE;
    //     }
    //     printf("Sent message: %s\n", query_list[i]); // Debugging statement
    // }
    // for (size_t i = 0; i < 3; i++) {
    //     int32_t err = read_res(fd);
    //     if (err) {
    //         goto L_DONE;
    //     }
    // }

    // new -> 

    std::vector<std::string> cmd;
    for(int i=1;i<argc;i++)
        cmd.push_back(argv[i]);

    printf("printing cmd vector args---> \n");
    for(int i=0;i<cmd.size();i++){
        printf("%s\n", cmd[i].c_str());
    }
    printf(">>>>>\n");
    int32_t err = send_req(fd, cmd);
    if (err)
        goto L_DONE;

    err = read_res(fd);
    if (err)
        goto L_DONE;
L_DONE:
    closesocket(fd);
    return 0;
}