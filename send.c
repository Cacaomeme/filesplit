/* -*- coding: utf-8-unix; -*-                                     */
/* FILENAME     :  send.c (Server Mode)                            */
/* DESCRIPTION  :  TCP Multi-Interface File Server                 */
/* USAGE        :  ./send.out [file_node1] [file_node2] ...        */
/* ----------------------------------------------------------------*/

#include "icslab2_net.h"
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>

#define NUM_TARGET_NODES 4

// ★同期用グローバル変数
pthread_mutex_t trigger_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  trigger_cond  = PTHREAD_COND_INITIALIZER;
int is_node1_active = 0; // Node1が接続され、送信中であることを示すフラグ

// サーバー設定をスレッドに渡すためのデータ構造
typedef struct {
    char local_ip[16];      // BindするローカルIP (例: 172.21.0.30)
    char target_name[16];   // 対象ノード名 (表示用: Node1など)
    char filename[256];     // 送信するファイル名
    int port;               // 待ち受けポート
} ServerConfig;

// ===================================================================
// サーバー用スレッド関数 (server_thread)
// 指定されたIPでListenし、接続が来たらファイルを送る
// ===================================================================
void* server_thread(void* arg) {
    ServerConfig *conf = (ServerConfig *)arg;
    int serv_sock, client_sock;
    struct sockaddr_in servAddr, clientAddr;
    socklen_t clientAddrLen;
    int fd, n;
    char buf[BUF_LEN];
    int yes = 1;
    int is_trigger_node = (strcmp(conf->target_name, "Node1") == 0); // Node1かどうか

    // ソケット作成
    if ((serv_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("[Thread] socket failed");
        return NULL;
    }

    // SO_REUSEADDR 設定
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    // Bind
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(conf->port);
    inet_pton(AF_INET, conf->local_ip, &servAddr.sin_addr.s_addr);

    if (bind(serv_sock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
        fprintf(stderr, "[Thread %s] Bind failed on %s: ", conf->target_name, conf->local_ip);
        perror("");
        close(serv_sock);
        return NULL;
    }

    // Listen
    if (listen(serv_sock, 5) < 0) {
        perror("[Thread] listen failed");
        close(serv_sock);
        return NULL;
    }

    printf("[Thread %s] Listening on %s:%d (File: %s)...\n", 
           conf->target_name, conf->local_ip, conf->port, conf->filename);

    // 接続待機ループ
    while (1) {
        clientAddrLen = sizeof(clientAddr);
        client_sock = accept(serv_sock, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if (client_sock < 0) {
            perror("[Thread] accept failed");
            continue;
        }

        printf("[Thread %s] Accepted connection from %s. ", 
               conf->target_name, inet_ntoa(clientAddr.sin_addr));

        // ★★★ 同期処理開始 ★★★
        if (is_trigger_node) {
            // Node1の場合: トリガーを引く
            printf("Triggering start!\n");
            pthread_mutex_lock(&trigger_mutex);
            is_node1_active = 1;
            pthread_cond_broadcast(&trigger_cond); // 待機中の他スレッドを一斉に起こす
            pthread_mutex_unlock(&trigger_mutex);
        } else {
            // Node1以外の場合: Node1が来るまで待つ
            printf("Waiting for Node1 trigger...\n");
            pthread_mutex_lock(&trigger_mutex);
            while (!is_node1_active) {
                pthread_cond_wait(&trigger_cond, &trigger_mutex);
            }
            pthread_mutex_unlock(&trigger_mutex);
            printf("[Thread %s] Trigger received! Starting transfer.\n", conf->target_name);
        }
        // ★★★ 同期処理終了 ★★★

        // ファイルオープン
        if ((fd = open(conf->filename, O_RDONLY)) < 0) {
            perror("[Thread] open file failed");
            close(client_sock);
            continue;
        }

        // ファイル送信処理
        long long total_bytes = 0;
        while ((n = read(fd, buf, BUF_LEN)) > 0) {
            if (write(client_sock, buf, n) != n) {
                perror("[Thread] write failed");
                break;
            }
            total_bytes += n;
        }

        printf("[Thread %s] Sent file '%s' (%lld bytes). Closing connection.\n", 
               conf->target_name, conf->filename, total_bytes);

        close(fd);
        close(client_sock);

        /* 修正: すぐにフラグを下ろさず、少し待つか、あるいはこの実験では下ろさない */
        /* 連続実験を行わないなら、以下のブロックをコメントアウトするのが一番確実です */
        
        // if (is_trigger_node) {
        //     pthread_mutex_lock(&trigger_mutex);
        //     is_node1_active = 0;
        //     pthread_mutex_unlock(&trigger_mutex);
        //     printf("[Thread %s] Reset trigger flag.\n", conf->target_name);
        // }
        
        /* 代替案: 全員が送信し終わるのを待つバリア同期が必要ですが、
           簡易的には sleep でごまかすことも可能です */
        if (is_trigger_node) {
             sleep(5); // 他のスレッドが動き出す時間を稼ぐ
             pthread_mutex_lock(&trigger_mutex);
             is_node1_active = 0;
             pthread_mutex_unlock(&trigger_mutex);
             printf("[Thread %s] Reset trigger flag.\n", conf->target_name);
        }
    }

    close(serv_sock);
    return NULL;
}

// ===================================================================
// サーバー起動関数
// ===================================================================
void start_multi_server(char **filenames) {
    
    ServerConfig configs[NUM_TARGET_NODES];
    pthread_t threads[NUM_TARGET_NODES];
    
    // Node3が持つ各インターフェースのIPアドレス
    const char *local_ips[] = {"172.21.0.30", "172.24.0.30", "172.27.0.30", "172.28.0.30"};
    const char *target_names[] = {"Node1", "Node2", "Node4", "Node5"};

    printf("\n--- Starting Multi-Interface File Server (Trigger: Node1) ---\n");
    int i;

    for (i = 0; i < NUM_TARGET_NODES; i++) {
        char *filename = filenames[i];
        
        if (strcmp(filename, "0") == 0) {
            printf("Skipping server for %s (file is '0')\n", target_names[i]);
            continue;
        }

        strncpy(configs[i].local_ip, local_ips[i], 15);
        strncpy(configs[i].target_name, target_names[i], 15);
        strncpy(configs[i].filename, filename, 255);
        configs[i].port = TCP_SERVER_PORT; // 10000

        if (pthread_create(&threads[i], NULL, server_thread, &configs[i]) != 0) {
            perror("pthread_create failed");
        }
    }

    for (i = 0; i < NUM_TARGET_NODES; i++) {
        if (strcmp(filenames[i], "0") != 0) {
            pthread_join(threads[i], NULL);
        }
    }
}

// ===================================================================
// メイン関数
// ===================================================================
int main(int argc, char** argv)
{
    if (argc < 5) {
        printf("Usage: %s [file_for_node1] [file_for_node2] [file_for_node4] [file_for_node5]\n", argv[0]);
        printf("Use '0' to skip a node.\n");
        return 1;
    }

    start_multi_server(&argv[1]);

    return 0;
}