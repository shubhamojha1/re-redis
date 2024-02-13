// cmd: 
// (wont work anymore) gcc -o server server.cpp -lws2_32 -lwsock32 -L $MinGW\lib; .\server
// g++ -I%BOOST_ROOT% -L%BOOST_ROOT%\stage\lib -o server server.cpp -lwsock32 -lws2_32; ./server
// #define _WIN32_WINNT 0x0601
#define _WIN32_WINNT _WIN32_WINNT_WIN10

#include <Winsock2.h>
#include <Ws2tcpip.h>

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
// #include <poll.h>


// Link with ws2_32.lib 
// for networking functionality in Windows
#pragma comment(lib, "Ws2_32.lib")

const size_t K_MAX_MSG = 4096;

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2, // mark the connection for deletion
};

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

static void state_req(Conn *conn);
static void state_res(Conn *conn);

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
    // abort();
    exit(EXIT_FAILURE); // Exit the program
}

static void fd_set_nb(SOCKET fd) {
    // errno = 0;
    // int flags = fcntl(fd, F_GETFL, 0);
    // if (errno) {
    //     die("fcntl error");
    //     return;
    // }

    // flags |= O_NONBLOCK;

    // errno = 0;
    // (void)fcntl(fd, F_SETFL, flags);
    // if (errno) {
    //     die("fcntl error");
    // }

    u_long mode = 1;
    if (ioctlsocket(fd, FIONBIO, &mode) == SOCKET_ERROR) {
        die("Failed to set non-blocking mode for socket");
    }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd+1); // resize vector
    }
    fd2conn[conn->fd] = conn; // put connection
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, SOCKET fd) {
    struct sockaddr_in client_addr = {};
    int socklen = sizeof(client_addr);
    SOCKET connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen); // accept 
    if (connfd == INVALID_SOCKET) {
        msg("accept() error");
        return -1;
    }

    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd); 
    // creating the struct Conn
    // struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    struct Conn *conn = new Conn;
    if (!conn) {
        close(connfd);
        return -1;
    }

    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}

// To handle client connection
// static void do_something(int connfd){
//     char rbuf[64] = {};
//     // ssize_t n = read(connfd, rbuf, sizeof(rbuf)-1);
//     // if (n<0) {
//     //     msg("read() error");
//     //     return;
//     // }
//     // printf("client says: %s\n", rbuf);

//     // char wbuf[] = "world";
//     // write(connfd, wbuf, strlen(wbuf));

//     int n = recv(connfd, rbuf, sizeof(rbuf)-1, 0);
//     if (n > 0) {
//         rbuf[n] = '\0'; // Null-terminate the string
//         printf("client says: %s\n", rbuf);

//     const char wbuf[] = "world";
//     send(connfd, wbuf, strlen(wbuf), 0);
//     } else if (n == 0) {
//         printf("The client closed the connection\n");
//     } else {
//         msg("recv() error");
//     }
// }

static int32_t read_full(SOCKET fd, char *buf, size_t n) {
    while (n > 0) {
        // ssize_t rv = read(fd, buf, n);
        int rv = recv(fd, buf, n, 0);
        // if (rv <= 0) {
        //     return -1; // error or unexpected EOF
        // }
        if (rv == 0) {
            fprintf(stderr, "EOF received\n");
            return -2;
        } else if(rv < 0) {
            // An error occurred
            int err = WSAGetLastError();
            fprintf(stderr, "recv failed with error: %d\n", err);
            return -1;
        }
        fprintf(stderr, "Received %d bytes\n", rv); // Debugging statement
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

const size_t K_MAX_ARGS = 1024;

static int32_t parse_req(const uint8_t *data, size_t len, 
                        std::vector<std::string> &out){
    // parsing command
    if (len < 4)
        return -1;
    uint32_t n = 0;
    memcpy(&n, &data[0], 4);

    if (n > K_MAX_ARGS)
        return -1;

    size_t pos = 4;
    while (n--){
        if(pos+4 > len){
            return -1;
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len) {
            return -1;
        }
        out.push_back(std::string((char*)&data[pos+4], sz));
        pos += 4+sz;
    }

    if (pos != len)
        return -1;

    return 0;
}

enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

static std::map<std::string, std::string> g_map;

static uint32_t do_get(const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen){
    if(!g_map.count(cmd[1]))
        return RES_NX;

    std::string &val = g_map[cmd[1]];
    assert(val.size() <= K_MAX_MSG);

    memcpy(res, val.data(), val.size());
    *reslen = (uint32_t)val.size();

    return RES_OK;
}

static uint32_t do_set(const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen){
    (void)res;
    (void)reslen;

    g_map[cmd[1]] = cmd[2];
    return RES_OK;
}

static uint32_t do_del(const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen){
    (void)res;
    (void)reslen;

    g_map.erase(cmd[1]);
    return RES_OK;
}

// 3 commands:  (get, set, del)
static int32_t do_request(
    const uint8_t *req, uint32_t reqlen,
     uint32_t *rescode, uint8_t *res, uint32_t *reslen
){
    std::vector<std::string> cmd;
    if (0!= parse_req(req, reqlen, cmd)){
        msg("bad request");
        return -1;
    }
    if (cmd.size() == 2 && cmd_is(cmd[0], "get")){
        *rescode = do_get(cmd, res, reslen);
    } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")){
        *rescode = do_set(cmd, res, reslen);
    } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")){
        *rescode = do_del(cmd, res, reslen);
    } else{
        // cmd not recognized
        *rescode = RES_ERR;
        const char *msg = "unknown cmd!!!";
        strcpy((char*)res, msg);
        *reslen = strlen(msg);
        return 0;
    }
    return 0;
}

static bool try_one_request(Conn *conn) {
    // try to parse request from the buffer
    if (conn->rbuf_size < 4) {
        // not enough data in the buffer. 
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > K_MAX_MSG) {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        // not enough data in buffer.
        return false; 
    }

    // got one request, do something with it
    // printf("client says: %.*s\n", len, &conn->rbuf[4]);
    uint32_t rescode = 0;
    uint32_t wlen = 0;
    int32_t err = do_request(&conn->rbuf[4], len, &rescode, 
                &conn->wbuf[4+4], &wlen);

    if (err) {
        conn->state = STATE_EMD;
        return false;
    }
    wlen += 4;
    
    // generating echoing response
    // memcpy(&conn->wbuf[0], &len, 4);
    // memcpy(&conn->wbuf[4], &conn->rbuf, len);
    // conn->wbuf_size = 4 + len;

    // remove the request from buffer
    /**
     * @brief frequent memmove is inefficient
            need better handling for production code
     * 
     */
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], &rescode, 4);
    conn->wbuf_size = 4 + len;

    size_t remain = conn->rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // change state
    conn->state - STATE_RES;
    state_res(conn);

    // continue outer loop if the request was fully processed
    return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn) {
    // try to fill the buffer
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do {
        size_t cap = sizeof(conn-> rbuf) - conn-> rbuf_size;
        rv = recv(conn->fd, (char *)&conn->rbuf[conn->rbuf_size], cap, 0);
    } while(rv < 0 && WSAGetLastError() == WSAEINTR);

    if (rv < 0) {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }
    if (rv == 0) {
        if (conn->rbuf_size > 0) {
            msg("unexpected EOF");
        } else {
            msg("EOF");
        }
        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    // Try to process requests one by one
    // "pipelining"
    while(try_one_request(conn)) {}
    return (conn->state == STATE_REQ);
}

static bool try_flush_buffer(Conn *conn) {
    ssize_t rv = 0;
    do {
        ssize_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = send(conn->fd, (char *)&conn->wbuf[conn->wbuf_sent], remain, 0);
        
    } while (rv < 0 && errno == WSAEINTR);

    if (rv < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
            return false;
    
    if (rv < 0) {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size) {
        // response fully sent, change state back
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    // still got some data in wbuf, could try to write again
    return true;
}

static void state_req(Conn *conn) {
    while(try_fill_buffer(conn)) {}
}

static void state_res(Conn *conn) {
    while (try_flush_buffer(conn)) {}
}

// state machine for client connections
static void connection_io(Conn *conn) {
    if (conn->state == STATE_REQ) {
        state_req(conn); 
    } else if(conn->state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0); // not expected
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
        die("socket() failed");
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
    // struct sockaddr_in addr = {};
    // addr.sin_family = AF_INET;
    // addr.sin_port = ntohs(1234);
    // addr.sin_addr.s_addr = ntohl(0); // wildcard address 0.0.0.0

    // int rv = bind(fd, (const sockaddr*)&addr, sizeof(addr));
    // if (rv) {
    //     die("bind()");
    // }

    // // listen for connections
    // rv = listen(fd, SOMAXCONN);
    // if (rv) {
    //     die("listen()");
    // }

    sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr("127.0.0.1");
    service.sin_port = htons(1234);
    if (bind(fd, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        die("bind() failed");
    }

    if (listen(fd, SOMAXCONN) == SOCKET_ERROR) {
        die("listen() failed");
    }

    // map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;

    // set the listen fd to nonblocking mode
    fd_set_nb(fd);

    // the event loop
    // std::vector<struct pollfd> poll_args;


    typedef struct pollfd {
    SOCKET fd;
    short events;
    short revents;
    } WSAPOLLFD, *PWSAPOLLFD, FAR *LPWSAPOLLFD;

    std::vector<WSAPOLLFD> poll_args;

    while (true) {
        // accept connection
        // printf("-----\nSERVER RUNNING\n-----\n");

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
        poll_args.clear();
        // listening fd put in first position, for convenience
        // struct pollfd pfd = {fd, POLLIN, 0};
        WSAPOLLFD listen_sock_pollfd = {fd, FD_READ, 0};
        // listen_sock_pollfd.fd = fd;
        // POLLIN (unix) <-> FD_READ (windows)
        // listen_sock_pollfd.events = POLLIN;
        poll_args.push_back(listen_sock_pollfd);
        
        // connection fds
        for (Conn *conn: fd2conn) {
            if(!conn) {
                continue;
            }
            // struct pollfd pfd = {};
            WSAPOLLFD pfd = {};
            pfd.fd = conn->fd;
            // pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = (conn->state == STATE_REQ) ? FD_READ : FD_WRITE;
            // POLLOUT (unix) <-> FD_WRITE (windows)
            // pfd.events = pfd.events | POLLERR;
            pfd.events = pfd.events | FD_CLOSE;
            // POLLERR (unix) <-> FD_CLOSE (windows)
            poll_args.push_back(pfd);
        }

        // poll for active fds
        // timeout arg not important (need to check)
        int timeout = 1000;
        // int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);

        // int rv = WSAPoll(poll_args.data(), (ULONG)poll_args.size(), timeout);
        // if (rv < 0) {
        //     die("WSAPoll failed");
        // }
         fd_set readSet;
         FD_ZERO(&readSet);
         FD_SET(fd, &readSet);

        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        int rv = select(fd + 1, &readSet, NULL, NULL, &tv);
        if(rv<0)
            die("select() failed");

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
    WSACleanup();
    return 0;
}


// // 
// g++ -D_WIN32_WINNT=0x0601 -I%BOOST_ROOT% -L%BOOST_ROOT%\stage\lib -o server server.cpp -lwsock32 -lws2_32 
// -lboost_system-mgw48-mt-d-1_55