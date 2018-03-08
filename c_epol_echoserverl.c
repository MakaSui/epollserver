// compile: gcc c_epol_echoserverl.c -o c_epol_echoserverl
// run: ./c_epol_echoserverl

#include <stdio.h>
#include <sys/socket.h>
#include <strings.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

void err_exit(char *msg);
int create_socket(const char *ip,int port);
int create_epollfd();
void set_nonblocking(int fd);
void handle_events(int epollfd, int op, int fd, int events);
void handle_accept(int epollfd, int fd);
char *handle_read(int epollfd, int fd, struct epoll_event client);
void handleWrite(char *readbuf, int fd);
void loop_process(int epollfd, int sockfd);


int main(int argc, char *argv[]) {
    int sockfd,epollfd;

    if(argc != 3)
        err_exit("usage: <IPaddress> <port>");

    int port = strtol(argv[2], NULL, 10);
    sockfd = create_socket(argv[1], port);           // create socket fd
    printf("socket create success!\n");
    fflush(stdout);
    epollfd = create_epollfd();                             // create epoll fd

    set_nonblocking(sockfd);                                // set non-blocking
    handle_events(epollfd, EPOLL_CTL_ADD, sockfd, EPOLLIN); // register

    for( ; ; ){
        loop_process(epollfd, sockfd);
    }

    close(sockfd);
    close(epollfd);

    return 0;
}

void err_exit(char *msg) {
    perror(msg);
    exit(1);
}

int create_socket(const char *ip, int port) {
    int sockfd, reuse;
    struct sockaddr_in servaddr;

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    if(inet_pton(PF_INET, ip, &servaddr.sin_addr) == -1)
        err_exit("inet_pton() error");

    if((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        err_exit("socket() error");

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1)
        err_exit("setsockopt() error");

    if(bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1)
        err_exit("bind() error");

    if(listen(sockfd, 1024) == -1)
        err_exit("listen() error");

    return sockfd;
}

int create_epollfd(){
    int epollfd;

    if((epollfd = epoll_create(1)) < 0)
        err_exit("epoll_create() error");

    return epollfd;
}

void set_nonblocking(int fd){
    int flags;

    if( (flags = fcntl(fd, F_GETFL,0)) < 0)
        err_exit("fcntl() error");

    if(fcntl(fd, F_SETFL, flags|O_NONBLOCK) < 0)
        err_exit("fcntl() error");
}


void handle_events(int epollfd, int op, int fd, int events){
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    if(epoll_ctl(epollfd, op, fd, &ev) < 0)
        err_exit("epoll_ctl error");
}

void handle_accept(int epollfd, int fd){
    int connfd;
    struct sockaddr_in cliaddr, peer;
    socklen_t clilen = sizeof(cliaddr);
    socklen_t alen = sizeof(peer);

    if((connfd = accept(fd,(struct sockaddr *) &cliaddr, &clilen)) < 0)
        err_exit("accept() error");

    if(getpeername(connfd, (struct sockaddr *)&peer, &alen) < 0)
        err_exit("getpeername() error");

    printf("accept a connection from %s:%d\n", inet_ntoa(cliaddr.sin_addr),cliaddr.sin_port);
    fflush(stdout);

    set_nonblocking(connfd);

    handle_events(epollfd, EPOLL_CTL_ADD, connfd, EPOLLIN);

}

char *handle_read(int epollfd, int fd, struct epoll_event client){
    char buf[4096];
    ssize_t n;
    while((n = read(fd,buf, sizeof(buf))) > 0){
        printf("read msg: %s,%d bytes\n", buf, (int)n);
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
        return NULL;
    }
    if (n < 0){
        printf("read() error\n");
    }

    handle_events(epollfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    printf("client shutdown！！！\n");
    fflush(stdout);

    return buf;

}

void handleWrite(char *readbuf, int fd){
    if(write(fd, readbuf, sizeof(readbuf)) < 0)  // echo
        err_exit("write() error");
}

void loop_process(int epollfd, int sockfd){
    int number,new_fd,new_events;
    const int max_epoll_events = 20;
    struct epoll_event events[100];
    int waitms = 10000;
    char *read_buf = NULL;

    if((number = epoll_wait(epollfd, events, max_epoll_events, waitms)) < 0)
        err_exit("epoll_wait() error");

    for (int i = 0; i < number; i++) {
        new_fd = events[i].data.fd;
        new_events = events[i].events;
        if(new_events & (EPOLLIN | EPOLLERR)) {
            if (new_fd == sockfd) {  // new accept
                printf("new accept!!!!\n");
                fflush(stdout);
                handle_accept(epollfd, new_fd);
            }
            else{ // read
                read_buf = handle_read(epollfd, new_fd, events[i]);
            }
        }
        else if(new_events & EPOLLOUT) { //write
            printf("handling epollout\n");
            handleWrite(read_buf, new_fd);
        }
        else{
            err_exit("unknow event");
        }

    }

}
