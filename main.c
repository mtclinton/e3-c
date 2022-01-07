#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

#define PORT "8885"   // Port we're listening on
#define MAXLINE		10

// Main
int main(void)
{
    struct sockaddr_storage their_addr;
    socklen_t addr_size;

    struct addrinfo hints, *res, *p;
    int sockfd, new_fd, epfd, nfds, n, conn_sock;

    int rv;

    int yes=1;        // For setsockopt() SO_REUSEADDR, below

    int MAX_EVENTS = 20;

    char line[MAXLINE];

    // !! don't forget your error checking for these calls !!

    // first, load up address structs with getaddrinfo():

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me


    if ((rv = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "e3-server: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) {
            continue;
        }

        // Lose the pesky "address already in use" error message
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) < 0) {
            close(sockfd);
            continue;
        }

        break;
    }

    freeaddrinfo(res); // All done with this

    // If we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "error getting binding socket\n");
        exit(1);
    }

    int opts;
    opts = fcntl(sockfd, F_GETFL);

    if(opts < 0) {
        perror("fcntl(sock, GETFL)");
        exit(1);
    }

    opts = opts | O_NONBLOCK;

    if(fcntl(sockfd, F_SETFL, opts) < 0) {
        perror("fcntl(sock, SETFL, opts)");
        exit(1);
    }

    // Listen
    if (listen(sockfd, 10) == -1) {
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }

    struct epoll_event ev, events[MAX_EVENTS];

    epfd = epoll_create1(0);

    if (epfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == sockfd) {
                addr_size = sizeof their_addr;
                // error here:
                printf("accept--\n");
                conn_sock = accept(sockfd, (struct sockaddr *) &their_addr, &addr_size);
                if (conn_sock == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                opts = fcntl(conn_sock, F_GETFL);

                if(opts < 0) {
                    perror("fcntl(conn_sock, GETFL)");
                    exit(1);
                }

                opts = opts | O_NONBLOCK;

                if(fcntl(conn_sock, F_SETFL, opts) < 0) {
                    perror("fcntl(conn_sock, SETFL, opts)");
                    exit(1);
                }

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = conn_sock;

                if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
                    perror("epoll_ctl: conn_sock");
                    exit(EXIT_FAILURE);
                }
            } else if(events[n].events & EPOLLIN) {
                if((new_fd = events[n].data.fd) < 0) continue;
                if((n = read(new_fd, line, MAXLINE)) < 0) {
                    if(errno == ECONNRESET) {
                        close(new_fd);
                        events[n].data.fd = -1;
                        printf("reading1 error");
                    } else {
                        printf("readline error");
                    }
                } else if(n == 0) {
                    printf("closed new_fd");
                    close(new_fd);
                    events[n].data.fd = -1;
                }

                printf("received data: %s\n", line);

                ev.data.fd = new_fd;
                ev.events = EPOLLOUT | EPOLLET;
                // errror caused by this :
                epoll_ctl(epfd, EPOLL_CTL_MOD, new_fd, &ev);
            }
            else if(events[n].events & EPOLLOUT) {
                printf("Out called");
                new_fd = events[n].data.fd;
                write(new_fd, line, n);

                printf("written data: %s\n", line);

                ev.data.fd = new_fd;
                ev.events = EPOLLIN | EPOLLET;
                epoll_ctl(epfd, EPOLL_CTL_MOD, new_fd, &ev);
            }
        }

    } // END for(;;)

    return 0;
}