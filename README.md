## filesplit.cの使い方
gcc filesplit.c -o split.out -lm　　
./split.out 分割したいファイル splitlist.txt

## splitlist.txtの使い方
各行ごとに分割の比率を指定、２行なら2つのファイルに分かれる。　　
正規化はfilesplit.cがやってくれれる。
