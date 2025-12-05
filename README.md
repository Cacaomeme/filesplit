# TCPフルメッシュネットワークマニュアル

5ノードフルメッシュネットワークを用いた、マルチパスファイル転送実験のプログラム群です。
Node3（送信元）からNode1（受信先）へ、「直接経路」と「Node2経由の経路」を使って並列にファイルを転送します。

## 1. ファイル構成と役割

| ファイル名 | 役割 | 実行場所 |
| :--- | :--- | :--- |
| **`send.c`** | **送信サーバー**。Node1からの接続をトリガーに、全経路へ一斉にファイルを送信します。 | **Node3** |
| **`tcp_echo_rooter.c`** | **中継ルーター**。Node1からの接続を受け、Node3へ接続してデータを中継します。 | **Node2** |
| **`receive_tcp.c`** | **受信クライアント**。複数の経路から同時にデータを受信し、保存します。 | **Node1** |
| **`filesplit.c`** | **分割ツール**。ファイルを指定比率で分割します。 | 任意 |

## 2. コンパイル方法

以下のコマンドですべての実行ファイルを作成します。

```bash
# 送信サーバー (Node3用) - スレッドライブラリが必要
gcc send.c -o send.out -lpthread

# 中継ルーター (Node2用)
gcc tcp_echo_rooter.c -o rooter.out

# 受信クライアント (Node1用)
gcc receive_tcp.c -o receive.out

# ファイル分割ツール - 数学ライブラリが必要
gcc filesplit.c -o split.out -lm
```

## 3. 実験準備 (データ作成)

1GBのファイルを作成し、5:5の比率で分割する例です。

```bash
# 1. 元データの作成 (1GB)
dd if=/dev/zero of=original.dat bs=1M count=1000

# 2. 分割比率ファイルの作成 (splitlist.txt)
# 各行ごとに分割の比率を指定。2行なら2つのファイルに分かれる。正規化は自動で行われる。echoコマンドで編集するもよし、直接いじってもよし
echo "5" > splitlist.txt
echo "5" >> splitlist.txt

# 3. 分割実行
./split.out original.txt splitlist.txt
# -> 1.txt, 2.txtといった順で分割生成される。
```

## 4. 実験手順

以下の順序で各ノードのプログラムを起動してください。

### Step 1: Node3 (送信サーバー)
ファイルを持って待機します。
```bash
# ./send.out [Node1用ファイル] [Node2用ファイル] 0 0
./send.out 1.txt 2.txt 0 0
```

### Step 2: Node2 (中継ルーター)
Node1からの接続を待ち、Node3へ中継します。
```bash
# ./rooter.out [接続先(Node3)のホスト名またはIP]
./rooter.out node3
```

### Step 3: Node1 (受信クライアント)
これを実行すると全経路での転送が一斉に開始されます。
```bash
# ./receive.out [保存ファイル名] [経由ルート] [直接ルート]
./receive.out result.txt node2 node3
```

## 5. 結果確認

Node1の実行結果にスループットが表示されます。
また、受信ファイルのサイズが元ファイルと同じか確認してください。

```bash
ls -lh result.txt
```
