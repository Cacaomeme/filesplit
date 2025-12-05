/*  -*- coding: utf-8-unix; -*-                                     */
/*  FILENAME     :  tcp_echo_serv.c                                 */
/*  DESCRIPTION  :  TCP Echo Server                                 */
/*                                                                  */
/*  VERSION      :                                                  */
/*  DATE         :  Sep 01, 2020                                    */
/*  UPDATE       :                                                  */
/*                                                                  */

#include "icslab2_net.h"

int
main(int argc, char** argv)
{
    char *server_ipaddr_str = "127.0.0.1";      /* サーバIPアドレス（文字列） */
    unsigned int port = TCP_SERVER_PORT;        /* ポート番号 */
    char* port_num_str = TCP_SERVER_PORT_STR;

    int     socks;                   /* ソケットディスクリプタ */
    int     sock0;                  /* 待ち受け用ソケットディスクリプタ */
    int     sock;                   /* ソケットディスクリプタ */
    struct sockaddr_in  myAddr; /* 自分用アドレス構造体 */
    struct sockaddr_in  clientAddr; /* クライアント用アドレス構造体 */
    struct sockaddr_in serverAddr;  /* サーバ＝相手用のアドレス構造体 */
    int     addrLen;                /* clientAddrのサイズ */

    struct addrinfo hints, *res;
	int err = 1;

    char    buf[BUF_LEN];           /* 受信バッファ */
    int     n;                      /* 受信バイト数 */
    int     isEnd = 0;              /* 終了フラグ，0でなければ終了 */

    int     yes = 1;                /* setsockopt()用 */
    struct in_addr addr;            /* アドレス表示用 */

    /* コマンドライン引数の処理 */
    if(argc == 2 && strncmp(argv[1], "-h", 2) == 0) {
        printf("Usage: %s [dst_ip_addr] [port]\n", argv[0]);
        return 0;
    }
    if(argc > 1)    /* 宛先を指定のIPアドレスにする。 portはデフォルト */
        server_ipaddr_str = argv[1];
    if(argc > 2)    /* 宛先を指定のIPアドレス、portにする */
        port = (unsigned int)atoi(argv[2]);



    /* IPアドレス（文字列）から変換 */
    if (inet_pton(AF_INET, server_ipaddr_str, &serverAddr.sin_addr.s_addr) == 1) {
		addr.s_addr = serverAddr.sin_addr.s_addr;
	} else {
		/* 解決 */
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;  /* TCP */

		err = getaddrinfo(server_ipaddr_str, port_num_str, &hints, &res);
		if (err != 0) {
			perror("getaddrinfo");
			return 1;
		}

		addr.s_addr = ((struct sockaddr_in*)(res->ai_addr))->sin_addr.s_addr;
	}

    /* 確認用：IPアドレスを文字列に変換して表示 */
    
    printf("ip address: %s\n", inet_ntoa(addr));
    printf("port#: %d\n", ntohs(serverAddr.sin_port));



    

    /* STEP 1: TCPソケットをオープンする */
    if((sock0 = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socke");
        return  1;
    }




    /* sock0のコネクションがTIME_WAIT状態でもbind()できるように設定 */
    setsockopt(sock0, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&yes, sizeof(yes));

    /* STEP 2: クライアントからの要求を受け付けるIPアドレスとポートを設定する */
    memset(&myAddr, 0, sizeof(myAddr));     /* ゼロクリア */
    myAddr.sin_family = AF_INET;                /* Internetプロトコル */
    myAddr.sin_port = htons(TCP_SERVER_PORT);   /* 待ち受けるポート */
    myAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* どのIPアドレス宛でも */

    /* STEP 3: ソケットとアドレスをbindする */
    if(bind(sock0, (struct sockaddr *)&myAddr, sizeof(myAddr)) < 0) {
        perror("bind");
        return  1;
    }

    /* STEP 4: コネクションの最大同時受け入れ数を指定する */
    if(listen(sock0, 5) != 0) {
        perror("listen");
        return  1;
    }

    while(!isEnd) {     /* 終了フラグが0の間は繰り返す */

        /* STEP 5: クライアントからの接続要求を受け付ける */
        printf("waiting connection...\n");
        addrLen = sizeof(clientAddr);
        sock = accept(sock0, (struct sockaddr *)&clientAddr, (socklen_t *)&addrLen);
        if(sock < 0) {
            perror("accept");
            return  1;
        }

        /* STEP 2: TCPソケットをオープン */
        socks = socket(AF_INET, SOCK_STREAM, 0);
        if(socks < 0) {
            perror("socket");
            close(sock);
            return  1;
        }

        if(err != 0 && connect(socks, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            perror("connect");
            return  1;
        } else if (err == 0 && connect(socks, res->ai_addr, res->ai_addrlen) < 0) {
            perror("connect");
            return -11;
        }



        /* 受信パケットの送信元IPアドレスとポート番号を表示 */        
        addr.s_addr = clientAddr.sin_addr.s_addr;
        printf("accepted:  ip address: %s, ", inet_ntoa(addr));
        printf("port#: %d\n", ntohs(clientAddr.sin_port));

        /* STEP 6: サーバーから受信 */
        while((n = read(socks, buf, BUF_LEN)) > 0) { /* 受信するたびに */

            /* STEP 7: 受信データをそのままクライアントに転送 */
            write(sock, buf, n);
            
            if(strncmp(buf, "quit", 4) == 0) {      /* "quit"なら停止 */
                isEnd = 1;                      
                break;
            }
        }

        /* STEP 8: 通信用のソケットのクローズ */
        close(sock);
        /* STEP 6: ソケットのクローズ */
        close(socks);
        printf("closed\n");
            
    }

    
    /* STEP 9: 待ち受け用ソケットのクローズ */
    close(sock0);  


    return  0;
}

/* Local Variables: */
/* compile-command: "gcc tcp_echo_server.c -o tcp_echo_server.out" */
/* End: */
