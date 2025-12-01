#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>

#define BUF_SZ (64 * 1024)

typedef struct {
    double frac;  /* 小数部分 */
    int idx;      /* 元のインデックス */
} frac_idx_t;

/* 小数部分の大きい順に並べる */
static int cmp_frac_desc(const void *a, const void *b)
{
    const frac_idx_t *A = a;
    const frac_idx_t *B = b;
    if (A->frac < B->frac) return 1;
    if (A->frac > B->frac) return -1;
    return (A->idx > B->idx) - (A->idx < B->idx); /* tie-break: idx 昇順 */
}

/* ratio ファイルを読み込み、重み配列を作成 */
static double *load_weights(const char *ratiofile, int *out_count)
{
    FILE *rf = fopen(ratiofile, "r");
    if (!rf) {
        perror("fopen ratio file");
        return NULL;
    }

    int cap = 16;
    int n = 0;
    double *weights = malloc(sizeof(double) * cap);
    if (!weights) {
        perror("malloc");
        fclose(rf);
        return NULL;
    }

    char line[512];
    while (fgets(line, sizeof(line), rf)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;           /* 前方空白スキップ */
        if (*p == '\0' || *p == '\n' || *p == '#') continue; /* 空行/コメント */

        char *tok = strtok(p, " \t\r\n");
        if (!tok) continue;

        char *endptr;
        double v = strtod(tok, &endptr);
        if (endptr == tok) continue;                   /* 数値でない行は無視 */
        if (v < 0) v = 0.0;                            /* 負は0扱い */

        if (n >= cap) {
            cap *= 2;
            double *tmp = realloc(weights, sizeof(double) * cap);
            if (!tmp) {
                perror("realloc");
                free(weights);
                fclose(rf);
                return NULL;
            }
            weights = tmp;
        }
        weights[n++] = v;
    }
    fclose(rf);

    if (n == 0) {
        fprintf(stderr, "no valid weights found in %s\n", ratiofile);
        free(weights);
        return NULL;
    }
    *out_count = n;
    return weights;
}

/* 各パートのバイト数を計算（正規化＋丸め処理） */
static off_t *compute_chunks(off_t filesize, const double *weights, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += weights[i];
    if (sum <= 0.0) {
        fprintf(stderr, "sum of weights is zero\n");
        return NULL;
    }

    off_t *want = calloc(n, sizeof(off_t));//とりあえず0で初期化
    double *exact = malloc(sizeof(double) * n);
    frac_idx_t *fi = malloc(sizeof(frac_idx_t) * n);
    if (!want || !exact || !fi) {
        perror("malloc");
        free(want);
        free(exact);
        free(fi);
        return NULL;
    }

    off_t base_sum = 0;
    for (int i = 0; i < n; i++) {
        exact[i] = (double)filesize * (weights[i] / sum);//ファイルサイズに対する各重みの割合を計算
        off_t base = (off_t)floor(exact[i]);//小数点以下切り捨て。切り捨てた分はあとで調整
        want[i] = base;
        base_sum += base;
        fi[i].frac = exact[i] - (double)base;//切り捨てた小数部分を保存
        fi[i].idx = i;
    }

    off_t rem = filesize - base_sum;//切り捨てた分の合計を計算
    if (rem > 0) {
        qsort(fi, n, sizeof(frac_idx_t), cmp_frac_desc);//正規化した端数の大きい順にソート
        for (off_t k = 0; k < rem; k++) {
            want[fi[k].idx] += 1;//端数の大きいものから順に1バイトずつ追加
        }
    }

    free(exact);
    free(fi);
    return want;
}

/* 指定サイズ配列 want[] に従って分割ファイルを書き出す */
static int split_file(FILE *inf, const off_t *want, int parts)
{
    unsigned char *buf = malloc(BUF_SZ);
    if (!buf) {
        perror("malloc");
        return -1;
    }

    for (int i = 0; i < parts; i++) {
        char outname[256];
        snprintf(outname, sizeof(outname), "%d.txt", i + 1);//出力ファイル名を作成
        FILE *outf = fopen(outname, "wb");
        if (!outf) {
            perror("fopen output");
            free(buf);
            return -1;
        }

        off_t remaining = want[i];
        while (remaining > 0) {
            size_t chunk = (size_t)(remaining > BUF_SZ ? BUF_SZ : remaining);//一度に読むサイズを決定、最大でBUF_SZバイト
            size_t rn = fread(buf, 1, chunk, inf);//入力ファイルからbufにchunkバイト読み込み、rnに実際に読んだバイト数を取得
            if (rn == 0) {
                if (feof(inf)) break;
                if (ferror(inf)) {
                    perror("fread");
                    fclose(outf);
                    free(buf);
                    return -1;
                }
            }
            size_t wn = fwrite(buf, 1, rn, outf);//bufから出力ファイルにrnバイト書き込み、wnに実際に書き込んだバイト数を取得
            if (wn != rn) {
                perror("fwrite");
                fclose(outf);
                free(buf);
                return -1;
            }
            remaining -= (off_t)wn;
        }
        /* 最後のファイルには "exit" を追記する */
        if (i == parts - 1) {
            const char *tail = "exit";
            if (fwrite(tail, 1, strlen(tail), outf) != strlen(tail)) {
                perror("fwrite(exit)");
                fclose(outf);
                free(buf);
                return -1;
            }
        }
        fclose(outf);
    }

    free(buf);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s inputfile ratio_list.txt\n", argv[0]);
        return 1;
    }
    const char *infile = argv[1];//分割するファイル名
    const char *ratiofile = argv[2];//比率ファイル名

    /* 入力ファイルを開きサイズを求める */
    FILE *inf = fopen(infile, "rb");
    if (!inf) {
        perror("fopen input");
        return 1;
    }
    if (fseeko(inf, 0, SEEK_END) != 0) {
        perror("fseeko");
        fclose(inf);
        return 1;
    }
    off_t filesize = ftello(inf);
    if (filesize < 0) {
        perror("ftello");
        fclose(inf);
        return 1;
    }
    rewind(inf);

    /* 比率ファイルを読み込む */
    int parts = 0;
    double *weights = load_weights(ratiofile, &parts);//ファイルから重みを抽出して配列に格納、partsに要素数を格納
    if (!weights) {
        fclose(inf);
        return 1;
    }

    /* 分割サイズを計算 */
    off_t *want = compute_chunks(filesize, weights, parts);
    if (!want) {
        free(weights);
        fclose(inf);
        return 1;
    }

    /* 分割ファイルを書き出す */
    int rc = split_file(inf, want, parts);

    free(weights);
    free(want);
    fclose(inf);
    return rc == 0 ? 0 : 1;
}