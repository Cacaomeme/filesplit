/*  -*- coding: utf-8-unix; -*-                                     */
/*  FILENAME     :  tcp_echo_serv.c                                 */
/*  DESCRIPTION  :  TCP Echo Server                                 */
/*                                                                  */
/*  VERSION      :                                                  */
/*  DATE         :  Sep 01, 2020                                    */
/*  UPDATE       :                                                  */
/*                                                                  */

#define _POSIX_C_SOURCE 200112L /* getaddrinfo, clock_gettime用 */
#include "icslab2_net.h"
#include <time.h>               /* clock_gettime, struct timespec */
#include <sys/stat.h>           /* fstat */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>              /* getaddrinfo用 */

#define MAX_EVENTS 30

int epoll_ctl_add_in(int epfd, int fd);

int main(int argc, char** argv)
{
    char **server_ipaddr_strs;
    unsigned int port = TCP_SERVER_PORT;
    char *filename = NULL;
    int fd = 1;                             /* 標準出力 */
    char *dummy_file = "HELLO.txt";               /* ダミーのリクエストメッセージ */

    int n_servers;
    int *serverSocks;
    int epfd;

    struct epoll_event events[MAX_EVENTS];
    int nfds;

    struct sockaddr_in  *serverAddrs;
    int     addrLen;                /* clientAddrのサイズ */

    char    buf[BUF_LEN];           /* 受信バッファ */
    int     n;                      /* 受信バイト数 */
    int     isEnd = 0;              /* 終了フラグ，0でなければ終了 */

    int     yes = 1;                /* setsockopt()用 */
    struct in_addr addr;            /* アドレス表示用 */
    int i;

    /* getaddrinfo用 */
    struct addrinfo hints, *res, *rp;
    int err;
    char port_str[16];

    double elapsed_sec;
    struct timespec start_time, end_time;
    struct stat info;
    double throughput_bps;

    /* コマンドライン引数の処理 */
    if(argc < 3) {
        printf("Usage: %s [output_file] [ip_address]\n", argv[0]);
        return 0;
    }

    printf("set outputfile: %s", argv[1]);
    filename = argv[1];
    fd = open(filename, O_CREAT | O_WRONLY, 0644);
    if(fd < 0) {
        perror("open");
        return 1;
    }

    n_servers = argc - 2;
    serverAddrs = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in) * n_servers);
    serverSocks = (int *)malloc(sizeof(int) * n_servers);
    server_ipaddr_strs = (char **)malloc(sizeof(char *) * n_servers);

    for (i = 0; i < n_servers; i++) {
        server_ipaddr_strs[i] = (char *)malloc(sizeof(char) * 16);
        strcpy(server_ipaddr_strs[i], argv[i + 2]);
    }

    /* ポート番号を文字列に変換 */
    snprintf(port_str, sizeof(port_str), "%d", port);

    for (i = 0; i < n_servers; i++) {
        /* getaddrinfo の設定 */
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;       /* IPv4 */
        hints.ai_socktype = SOCK_STREAM; /* TCP */

        /* ホスト名(またはIP)とポートからアドレス情報を解決 */
        err = getaddrinfo(server_ipaddr_strs[i], port_str, &hints, &res);
        if (err != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
            return 1;
        }

        /* 解決されたアドレスのリストを順に試す */
        for (rp = res; rp != NULL; rp = rp->ai_next) {
            serverSocks[i] = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (serverSocks[i] < 0) continue;

            if (connect(serverSocks[i], rp->ai_addr, rp->ai_addrlen) != -1) {
                break; /* 接続成功 */
            }

            close(serverSocks[i]); /* 失敗したら閉じる */
        }

        if (rp == NULL) { /* どのアドレスにも接続できなかった */
            fprintf(stderr, "Could not connect to %s\n", server_ipaddr_strs[i]);
            return 1;
        }

        freeaddrinfo(res); /* メモリ解放 */
    }

    epfd = epoll_create(MAX_EVENTS);
    if (epfd < 0) {
        perror("epoll_create");
        return 1;
    }

    for (i = 0; i < n_servers; i++) {
        if (epoll_ctl_add_in(epfd, serverSocks[i]) != 0) {
            perror("epoll_ctrl_add_in");
            return 1;
        }
    }

    clock_gettime(CLOCK_REALTIME, &start_time);

    int active_connections = n_servers; /* アクティブな接続数 */

    while(active_connections > 0) {
        nfds = epoll_wait(epfd, events, MAX_EVENTS, 60000);

        if (nfds < 0) {
            perror("epoll_wait");
            break;
        }
        if (nfds == 0) {
            fprintf(stderr, "Timeout\n");
            break;
        }

        for (i = 0; i < nfds; i++) {
            int sock_fd = events[i].data.fd;
            n = read(sock_fd, buf, BUF_LEN);
            
            if (n > 0) {
                /* データ受信 */
                write(fd, buf, n);
            } else {
                /* 切断 (n=0) またはエラー (n<0) */
                /* 監視対象から削除 */
                epoll_ctl(epfd, EPOLL_CTL_DEL, sock_fd, NULL);
                /* ソケットを閉じる (後でまとめて閉じる処理があるなら二重クローズに注意) */
                /* ここで閉じると、下のループでの close(serverSocks[i]) でエラーになるが、
                   通常は無視しても問題ないか、あるいは管理フラグを立てる */
                close(sock_fd); 
                active_connections--;
            }
        }
    }

    clock_gettime(CLOCK_REALTIME, &end_time);

    fstat(fd, &info);

    /* 既にループ内で閉じているので、ここの close ループは削除するか、
       エラーチェックを外すのが安全です。
       簡易的には、二重クローズを防ぐために以下のように修正します。 */
    // for (i = 0; i < n_servers; i++) {
    //     close(serverSocks[i]); 
    // }
    
    close(fd);

    free(serverAddrs);
    free(serverSocks);
    for (i = 0; i < n_servers; i++) {
        free(server_ipaddr_strs[i]);
    }
    free(server_ipaddr_strs);

    /* tv_nsec (ナノ秒) を使用するように修正 */
    elapsed_sec = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    if (elapsed_sec > 0) {
        throughput_bps = (info.st_size * 8.0) / elapsed_sec;
    } else {
        throughput_bps = 0.0;
    }

    printf("Total Bytes Transferred : %ld bytes\n", info.st_size); /* %lld -> %ld */
    printf("Total Elapsed Time      : %.6f sec\n", elapsed_sec);
    printf("Effective Throughput    : %.3f Mbps\n", throughput_bps / 1000000.0);

    return  0;
}

int epoll_ctl_add_in(int epfd, int fd)
{
    struct epoll_event ev; /* イベント */

    memset(&ev, 0, sizeof(ev)); /* 0クリア */
    ev.events = EPOLLIN;        /* read()可能というイベント */
    ev.data.fd = fd;            /* 関連付けるfd */
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl");
        return 1;
    }
    return 0;
}
