// slot_3x4_integrated_fx.c
// gcc slot_3x4_integrated_fx.c -o slot && ./slot
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <stdarg.h>   // ★ 가변 인자(va_list) 쓰기 위해 필요

static void cursor_hide(void);
static void cursor_show(void);
static void screen_clear(void);
static void gotoxy(int r, int c);

#define ROWS 3
#define COLS 4

#define START_SPINS 10
#define MIN_NUM 1
#define MAX_NUM 9
#define WEIGHT_BASE 1

// ── 상점/아이템 비용 및 가중치 증가값 ──
#define COST_SPIN_PLUS1 30        // 스핀 +1 가격
#define COST_BOOST_1_4 10        // 1~4 확률 업 가격
#define COST_BOOST_5_9 20        // 5~9 확률 업 가격
#define COST_444_PROTECT 15       // 444 보호 부적 가격

#define BIAS_INC_LOW 5           // 1~4 가중치 증가량
#define BIAS_INC_HIGH 10         // 5~9 가중치 증가량

// ── 랭킹 파일 ──
#define RANK_FILE "rankings.csv"
#define MAX_RANK_ROWS 1000

// 애니메이션 속도 조절용 (원하면 상점에서 바꿔도 가능)
int SPIN_FRAMES = 30;   // 회전 프레임 수 (많을수록 오래 돎)
int SPIN_DELAY_MS = 70; // 프레임 간 딜레이(ms) (클수록 느림)

// 각 스핀에서 당첨 메시지를 찍을 시작 줄 (원하면 숫자 조절 가능)
int g_msg_row = 1;

// ===== 콘솔 UI(중앙 정렬/커서/색/크기) =====
#if defined(_WIN32)
#include <conio.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#endif

#define ANSI_RESET   "\x1b[0m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_DIM     "\x1b[2m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_WHITE   "\x1b[37m"
#define ANSI_HIDE    "\x1b[?25l"
#define ANSI_SHOW    "\x1b[?25h"
#define ANSI_CLEAR   "\x1b[2J"
#define ANSI_HOME    "\x1b[H"
#define ANSI_BLUE_   "\x1b[38;5;67m"

static void get_terminal_size(int* rows, int* cols);

void gotoxyflash_hit_lines(int row, int col)
{
    const char* msg = "시작하려면 Enter를 누르세요";

    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0) {
            gotoxy(row, col);
            printf(ANSI_BLUE_ "%s" ANSI_RESET, msg);
        }
        else {
            gotoxy(row, col);
            printf("                               ");
        }

#if defined(_WIN32)
        Sleep(350);
#else
        usleep(350000);
#endif
    }

    gotoxy(row, col);
    printf(ANSI_BLUE_ "%s" ANSI_RESET, msg);
}


static void get_terminal_size(int* rows, int* cols) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    *rows = w.ws_row; *cols = w.ws_col;
#endif
}
static void gotoxy(int r, int c) {
    printf("\x1b[%d;%dH", r, c);
}
static void enable_vt_mode(void) {
#if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0; GetConsoleMode(hOut, &mode);
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
#endif
}
static void screen_clear(void) { fputs(ANSI_CLEAR ANSI_HOME, stdout); }
static void cursor_hide(void) { fputs(ANSI_HIDE, stdout); }
static void cursor_show(void) { fputs(ANSI_SHOW, stdout); }

// 콘솔 화면 가로 중앙 정렬로 문자열 출력
static void print_centered(int row, const char* text) {
    int termR, termC;
    get_terminal_size(&termR, &termC);
    int len = (int)strlen(text);
    int col = (termC - len) / 2;
    if (col < 1) col = 1;
    gotoxy(row, col);
    printf("%s", text);
}

// 포맷 문자열을 중앙에 출력하고, 다음 줄로 내려가도록 관리
static void log_centered_msg(const char* fmt, ...) {
    char buf[128];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    print_centered(g_msg_row, buf);
    g_msg_row++;   // 다음 메시지는 한 줄 아래에 찍히도록
}


// ===== 사운드(효과음) =====
#if defined(_WIN32)
static void sfx_spin_start(void) { Beep(880, 60); Beep(1175, 60); }
static void sfx_line_win(void) { Beep(1319, 80); Beep(1568, 90); }
static void sfx_big_win(void) { Beep(1760, 120); Beep(2093, 150); Beep(2637, 180); }
static void sfx_lose_round(void) { Beep(330, 120); Beep(247, 180); }
#else
static void sfx_spin_start(void) { printf("\a"); fflush(stdout); }
static void sfx_line_win(void) { printf("\a"); fflush(stdout); }
static void sfx_big_win(void) { printf("\a\a"); fflush(stdout); }
static void sfx_lose_round(void) { /* no-op */ }
#endif

// ===== 확률/라인 유틸 (STICKY & GIMME) =====
#define STICKY_H 35         // [%] 가로로 이전 칸 복사 확률
#define STICKY_V 20         // [%] 세로로 위 칸 복사 확률
#define GIMME_LINE_PROB 18  // [%] 한 스핀에 '보장 라인' 1개 만들 확률

static inline int roll100(void) { return rand() % 100; }

typedef struct { int len; int r[4]; int c[4]; } Line;     // 라인 좌표
typedef struct { char name[20]; Line L; } NamedLine;      // (옵션) 이름 포함

// 모든 라인(가로3, 세로4, 대각4=총11) 구성
static void build_all_lines(Line L[11]) {
    int k = 0;
    for (int r = 0; r < ROWS; r++) { Line t = { 4,{r,r,r,r},{0,1,2,3} }; L[k++] = t; }
    for (int c = 0; c < COLS; c++) { Line t = { 3,{0,1,2},{c,c,c} };   L[k++] = t; }
    Line d1 = { 3,{0,1,2},{0,1,2} };
    Line d2 = { 3,{0,1,2},{1,2,3} };
    Line d3 = { 3,{0,1,2},{3,2,1} };
    Line d4 = { 3,{0,1,2},{2,1,0} };
    L[k++] = d1; L[k++] = d2; L[k++] = d3; L[k++] = d4;
}

// ===== 유틸 =====
static int rand_range(int a, int b) { return a + rand() % (b - a + 1); }

// weights[1]~weights[9] 사용해서 숫자 뽑기
static int pick_number_by_weights(const int* weights) {
    int sum = 0; for (int n = 1; n <= 9; n++) sum += weights[n];
    int r = rand_range(1, sum), acc = 0;
    for (int n = 1; n <= 9; n++) { acc += weights[n]; if (r <= acc) return n; }
    return 9;
}

static void wait_enter(void) {
    int ch;
    int termR, termC;
    get_terminal_size(&termR, &termC);

    print_centered(5, "Enter: 스핀  |  s: 상점  |  q: 종료");

    // ---- 입력 위치를 중앙으로 배치 ----
    gotoxy(6, termC / 2);   // ← 여기를 중앙으로

    ch = getchar();
    if (ch == '\n') { return; }
    if (ch == 's' || ch == 'S' || ch == 'q' || ch == 'Q') {
        ungetc(ch, stdin);
        return;
    }
    while (ch != '\n' && ch != EOF) ch = getchar();
}

// 결과 화면에서 '엔터 눌러 넘어가기' 전용 (완전 중앙정렬)
static void wait_result_enter(void) {
    int termR, termC;
    get_terminal_size(&termR, &termC);

    const char* msg = "계속하려면 Enter를 누르세요...";
    int row = termR / 2 + 8;   // 보드 아래쪽 자연스럽게 위치 (원하면 수정)

    print_centered(row, msg);

    int ch;
    do {
        ch = getchar();
    } while (ch != '\n' && ch != EOF);
}


// 디버그/라인 메시지용 일시 정지 (완전 중앙정렬)
static void wait_log_enter(void) {
    int termR, termC;
    get_terminal_size(&termR, &termC);

    const char* msg = "(Enter를 누르면 계속 진행합니다...)";

    // 보드 아래 자연스러운 위치 (원하면 조정 가능)
    int row = termR / 2 + 10;

    print_centered(row, msg);

    int ch;
    do {
        ch = getchar();
    } while (ch != '\n' && ch != EOF);
}




// ===== 보드 출력(중앙 정렬) =====
static int board_text_width(void) { return 1 + 1 + (COLS * 1) + (COLS - 1) * 3 + 1; } // "│ " + n + " | " + " │"
static int board_text_height(void) { return 1 + ROWS + (ROWS - 1) + 1; }          // top + rows + h-lines + bottom

static void print_board_centered(int b[ROWS][COLS], int top_row, int left_col) {
    int r0 = top_row, c0 = left_col;
    gotoxy(r0 + 0, c0); puts("┌───────────────┐");
    for (int r = 0; r < ROWS; r++) {
        gotoxy(r0 + 1 + r * 2, c0); printf("│ ");
        for (int c = 0; c < COLS; c++) {
            printf("%d", b[r][c]);
            if (c < COLS - 1) printf(" | ");
        }
        printf(" │");
        if (r < ROWS - 1) { gotoxy(r0 + 2 + r * 2, c0); puts("│---+---+---+---│"); }
    }
    gotoxy(r0 + (1 + (ROWS - 1) * 2) + 1, c0); puts("└───────────────┘");
}

// ===== 보드 생성(STICKY + GIMME) =====
static void generate_board(int board[ROWS][COLS], int weights[10]) {
    // 1) STICKY 기반 채우기
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int num;
            if (c > 0 && roll100() < STICKY_H) {
                num = board[r][c - 1];
            }
            else if (r > 0 && roll100() < STICKY_V) {
                num = board[r - 1][c];
            }
            else {
                num = pick_number_by_weights(weights);
            }
            board[r][c] = num;
        }
    }
    // 2) 낮은 확률로 보장 라인 1개 심기(444 회피)
    if (roll100() < GIMME_LINE_PROB) {
        Line lines[11]; build_all_lines(lines);
        int pick = rand_range(0, 10);
        int lucky = rand_range(1, 9);
        if (lucky == 4) lucky = 5; // 444 라운드 무효 회피
        Line L = lines[pick];
        for (int i = 0; i < L.len; i++) board[L.r[i]][L.c[i]] = lucky;
    }
}

// ===== 애니메이션: 스핀 프레임 =====
static void spin_animation_frames(int board[ROWS][COLS], int weights[10], int frames, int delay_ms) {
    int termR, termC; get_terminal_size(&termR, &termC);
    int BW = board_text_width(), BH = board_text_height();
    int top = (termR - BH) / 2; if (top < 1) top = 1;
    int left = (termC - BW) / 2; if (left < 1) left = 1;

    for (int f = 0; f < frames; f++) {
        int lock_col = (f * (COLS + 1)) / frames;
        int temp[ROWS][COLS];
        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                if (c < lock_col) temp[r][c] = board[r][c];
                else temp[r][c] = pick_number_by_weights(weights);
            }
        }
        screen_clear(); cursor_hide();
        print_centered(2, " 스핀 중... ");
        printf(ANSI_CYAN ANSI_BOLD);
        print_board_centered(temp, top, left);
        printf(ANSI_RESET);
        cursor_show();
#if defined(_WIN32)
        Sleep(delay_ms);
#else
        usleep(delay_ms * 1000);
#endif
    }
    screen_clear(); cursor_hide();
    print_centered(2, " 스핀 결과 ");
    printf(ANSI_CYAN ANSI_BOLD);
    print_board_centered(board, top, left);
    printf(ANSI_RESET);
    cursor_show();
}

// ===== 채점/라인 판정 =====
static int all_equal_arr(const int* a, int len) {
    for (int i = 1; i < len; i++) if (a[i] != a[0]) return 0;
    return 1;
}

static int score_lines(int b[ROWS][COLS], int verbose, int* out_count777, int* out_count444) {
    int gained = 0, cnt7 = 0, cnt4 = 0;

    // 가로(길이4)
    for (int r = 0; r < ROWS; r++) {
        int line[COLS];
        for (int c = 0; c < COLS; c++) line[c] = b[r][c];
        if (all_equal_arr(line, COLS)) {
            if (line[0] == 4) {
                cnt4++;
                if (verbose) log_centered_msg("가로 %d줄 444: %d%d%d%d  → (444는 라운드 특수)\n", r + 1, line[0], line[1], line[2], line[3]);
                wait_log_enter();
            }
            else {
                int add = line[0] * COLS;
                gained += add;
                if (verbose) log_centered_msg("가로 %d줄 당첨: %d%d%d%d  → +%d\n", r + 1, line[0], line[1], line[2], line[3], add);
                wait_log_enter();
            }
            if (line[0] == 7) cnt7++;
        }
    }
    // 세로(길이3)
    for (int c = 0; c < COLS; c++) {
        int line[ROWS];
        for (int r = 0; r < ROWS; r++) line[r] = b[r][c];
        if (all_equal_arr(line, ROWS)) {
            if (line[0] == 4) {
                cnt4++;
                if (verbose) log_centered_msg("세로 %d줄 444: %d%d%d    → (444는 라운드 특수)\n", c + 1, line[0], line[1], line[2]);
                wait_log_enter();
            }
            else {
                int add = line[0] * ROWS;
                gained += add;
                if (verbose) log_centered_msg("세로 %d줄 당첨: %d%d%d    → +%d\n", c + 1, line[0], line[1], line[2], add);
                wait_log_enter();
            }
            if (line[0] == 7) cnt7++;
        }
    }
    // 대각선 4개(길이3)
    int d1[3] = { b[0][0],b[1][1],b[2][2] };
    int d2[3] = { b[0][1],b[1][2],b[2][3] };
    int d3[3] = { b[0][3],b[1][2],b[2][1] };
    int d4[3] = { b[0][2],b[1][1],b[2][0] };
    struct { const char* name; int* arr; } di[4] = {
        {"대각선(↘)#1", d1},{"대각선(↘)#2", d2},{"대각선(↙)#1", d3},{"대각선(↙)#2", d4}
    };
    for (int i = 0; i < 4; i++) {
        int* L = di[i].arr;
        if (all_equal_arr(L, 3)) {
            if (L[0] == 4) {
                cnt4++;
                if (verbose) log_centered_msg("%s 444 당첨: %d%d%d      → (444는 라운드 특수)\n", di[i].name, L[0], L[1], L[2]);
                wait_log_enter();
            }
            else {
                int add = L[0] * 3;
                gained += add;
                if (verbose) log_centered_msg("%s 당첨: %d%d%d      → +%d\n", di[i].name, L[0], L[1], L[2], add);
                wait_log_enter();
            }
            if (L[0] == 7) cnt7++;
        }
    }

    if (out_count777) *out_count777 = cnt7;
    if (out_count444) *out_count444 = cnt4;
    return gained;
}

// ===== 당첨 라인 깜빡이기(하이라이트) =====
typedef struct { int len; int r[4]; int c[4]; int value; } HitLine;

static int collect_hit_lines(int b[ROWS][COLS], HitLine out[], int maxn) {
    Line Ls[11]; build_all_lines(Ls);
    int k = 0;
    for (int i = 0; i < 11 && k < maxn; i++) {
        Line L = Ls[i];
        int v = b[L.r[0]][L.c[0]], ok = 1;
        for (int j = 1; j < L.len; j++) if (b[L.r[j]][L.c[j]] != v) { ok = 0; break; }
        if (ok) {
            out[k].len = L.len; out[k].value = v;
            for (int j = 0; j < L.len; j++) { out[k].r[j] = L.r[j]; out[k].c[j] = L.c[j]; }
            k++;
        }
    }
    return k;
}

static void flash_hit_lines(int b[ROWS][COLS], int times, int delay_ms) {
    HitLine hits[16]; int n = collect_hit_lines(b, hits, 16);
    if (n == 0) return;

    int termR, termC; get_terminal_size(&termR, &termC);
    int BW = board_text_width(), BH = board_text_height();
    int top = (termR - BH) / 2; if (top < 1) top = 1;
    int left = (termC - BW) / 2; if (left < 1) left = 1;

    for (int t = 0; t < times; t++) {
        // 컬러 on
        screen_clear(); cursor_hide();
        print_board_centered(b, top, left);
        for (int i = 0; i < n; i++) {
            const char* col = (hits[i].value == 7) ? ANSI_YELLOW : ANSI_GREEN;
            for (int j = 0; j < hits[i].len; j++) {
                int rr = hits[i].r[j], cc = hits[i].c[j];
                int row = top + 1 + rr * 2;
                int colx = left + 2 + cc * 4;
                gotoxy(row, colx);
                printf("%s%d%s", col, b[rr][cc], ANSI_RESET);
            }
        }
        cursor_show();
#if defined(_WIN32)
        Sleep(delay_ms);
#else
        usleep(delay_ms * 1000);
#endif

        // 컬러 off
        screen_clear(); cursor_hide();
        print_board_centered(b, top, left);
        cursor_show();
#if defined(_WIN32)
        Sleep(delay_ms);
#else
        usleep(delay_ms * 1000);
#endif
    }
}

// ===== 상점 =====
static void show_weights(const int* w) {
    printf("현재 가중치(1~9): ");
    for (int n = MIN_NUM; n <= MAX_NUM; n++) printf("%d:%d ", n, w[n]);
    puts("");
}

static void shop_menu(int* score, int* spins, int* weights, int* num444Protect) {
    // 현재 가격을 유지하도록 static 변수 사용 (프로그램 실행 중 값 유지)
    static int cur_cost_spin_plus1 = COST_SPIN_PLUS1; // 50 -> 다음엔 +50 증가
    static int cur_cost_boost_1_4 = COST_BOOST_1_4;  // 20 -> 구매시 *2
    static int cur_cost_boost_5_9 = COST_BOOST_5_9;  // 40 -> 구매시 *2
    // 444 보호 부적은 고정 가격(15)
    const int fixed_cost_444 = COST_444_PROTECT;

    while (1) {
        screen_clear();
        cursor_hide();

        int termR, termC;
        get_terminal_size(&termR, &termC);

        // 상점을 화면 중앙 근처에 배치할 기준 줄
        int base = termR / 2 - 7;
        if (base < 1) base = 1;

        char buf[256];

        print_centered(base, "=====  상점 (SHOP)  =====");
        sprintf(buf, "점수: %d   |   남은 스핀: %d", *score, *spins);
        print_centered(base + 1, buf);

        // 현재 가격을 동적으로 보여줌
        sprintf(buf, "1) 스핀 +1                 (가격 %d점)  [구매시 +%d점 증가]", cur_cost_spin_plus1, COST_SPIN_PLUS1);
        print_centered(base + 3, buf);

        sprintf(buf, "2) 1~4 확률 증가 (각 +%d)   (가격 %d점)   [구매시 가격 x2]", BIAS_INC_LOW, cur_cost_boost_1_4);
        print_centered(base + 4, buf);

        sprintf(buf, "3) 5~9 확률 증가 (각 +%d)  (가격 %d점)   [구매시 가격 x2]", BIAS_INC_HIGH, cur_cost_boost_5_9);
        print_centered(base + 5, buf);

        sprintf(buf, "4) 444 보호 부적 (1회성)   (가격 %d점)   [고정]", fixed_cost_444);
        print_centered(base + 6, buf);

        print_centered(base + 8, "0) 나가기");

        // 가중치 정보도 중앙에
        char wbuf[256] = { 0 };
        char tmp[32];
        strcpy(wbuf, "현재 가중치(1~9): ");
        for (int n = MIN_NUM; n <= MAX_NUM; n++) {
            sprintf(tmp, "%d:%d ", n, weights[n]);
            strcat(wbuf, tmp);
        }
        print_centered(base + 10, wbuf);
        print_centered(base + 12, "선택을 입력하고 Enter를 누르세요.");

        cursor_show();
        gotoxy(base + 13, termC / 2 - 3);  // 입력 커서를 중앙 근처로

        int sel;
        if (scanf("%d", &sel) != 1) {
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');  // 버퍼 정리

        if (sel == 0) {
            // 상점 종료 → 화면 지우고 메인으로 복귀
            screen_clear();
            return;
        }
        else if (sel == 1) {
            if (*score < cur_cost_spin_plus1) {
                print_centered(base + 15, "점수가 부족합니다.");
            }
            else {
                *score -= cur_cost_spin_plus1;
                (*spins) += 1;
                // 구매 후 가격은 고정 증가량만큼 증가 (+5)
                cur_cost_spin_plus1 += COST_SPIN_PLUS1;
                sprintf(buf, "스핀 +1! 남은 스핀: %d  |  점수: %d  |  다음 가격: %d", *spins, *score, cur_cost_spin_plus1);
                print_centered(base + 15, buf);
            }
        }
        else if (sel == 2) {
            if (*score < cur_cost_boost_1_4) {
                print_centered(base + 15, "점수가 부족합니다.");
            }
            else {
                *score -= cur_cost_boost_1_4;
                for (int n = 1; n <= 4; n++) weights[n] += BIAS_INC_LOW;
                // 구매 후 가격은 2배로 증가
                cur_cost_boost_1_4 *= 2;
                sprintf(buf, "1~4 확률 증가! (각 +%d)  |  점수: %d  |  다음 가격: %d", BIAS_INC_LOW, *score, cur_cost_boost_1_4);
                print_centered(base + 15, buf);
            }
        }
        else if (sel == 3) {
            if (*score < cur_cost_boost_5_9) {
                print_centered(base + 15, "점수가 부족합니다.");
            }
            else {
                *score -= cur_cost_boost_5_9;
                for (int n = 5; n <= 9; n++) weights[n] += BIAS_INC_HIGH;
                // 구매 후 가격은 2배로 증가
                cur_cost_boost_5_9 *= 2;
                sprintf(buf, "5~9 확률 증가! (각 +%d)  |  점수: %d  |  다음 가격: %d", BIAS_INC_HIGH, *score, cur_cost_boost_5_9);
                print_centered(base + 15, buf);
            }
        }
        else if (sel == 4) {
            if (*score < fixed_cost_444) {
                print_centered(base + 15, "점수가 부족합니다.");
            }
            else {
                *score -= fixed_cost_444;
                (*num444Protect)++;
                sprintf(buf, "444 보호 부적 획득! (현재 남은 부적 %d개)  |  점수: %d", *num444Protect, *score);
                print_centered(base + 15, buf);
            }
        }
        else {
            print_centered(base + 15, "잘못된 선택입니다. 0~4 중에서 선택하세요.");
        }

        // 상점 내에서 메시지 한 번 보고 넘어가고 싶으면 잠깐 대기
        print_centered(base + 22, "계속하려면 Enter를 누르세요...");
        int ch2;
        do { ch2 = getchar(); } while (ch2 != '\n' && ch2 != EOF);
    }
}

// ===== 랭킹 =====
typedef struct { char name[64]; int score; char when[32]; } RankRow;

static void save_ranking(const char* name, int score) {
    FILE* fp = fopen(RANK_FILE, "a");
    if (!fp) return;
    time_t t = time(NULL); struct tm* lt = localtime(&t);
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", lt);
    fprintf(fp, "\"%s\",%d,%s\n", name, score, ts);
    fclose(fp);
}
static int cmp_desc(const void* a, const void* b) {
    const RankRow* ra = (const RankRow*)a, * rb = (const RankRow*)b;
    return rb->score - ra->score;
}
static void show_top10(void) {
    FILE* fp = fopen(RANK_FILE, "r");
    if (!fp) {
        screen_clear();
        print_centered(10, "랭킹 데이터가 없습니다.");
        print_centered(12, "Enter를 누르면 돌아갑니다.");
        int ch; do { ch = getchar(); } while (ch != '\n' && ch != EOF);
        return;
    }

    RankRow* arr = (RankRow*)malloc(sizeof(RankRow) * MAX_RANK_ROWS);
    int n = 0;
    char line[256];

    while (fgets(line, sizeof(line), fp) && n < MAX_RANK_ROWS) {
        char* p1 = strchr(line, '\"');
        char* p2 = p1 ? strchr(p1 + 1, '\"') : NULL;
        if (!p1 || !p2) continue;

        int len = (int)(p2 - p1 - 1);
        if (len <= 0 || len >= 63) continue;
        strncpy(arr[n].name, p1 + 1, len);
        arr[n].name[len] = '\0';

        char* pc = strchr(p2 + 1, ',');
        if (!pc) continue;
        arr[n].score = atoi(pc + 1);

        char* pc2 = strchr(pc + 1, ',');
        if (pc2) {
            char* pnl = strchr(pc2 + 1, '\n');
            int wlen = pnl ? (int)(pnl - (pc2 + 1)) : (int)strlen(pc2 + 1);
            if (wlen > 31) wlen = 31;
            strncpy(arr[n].when, pc2 + 1, wlen);
            arr[n].when[wlen] = '\0';
        }
        else {
            strcpy(arr[n].when, "-");
        }

        n++;
    }
    fclose(fp);

    if (n == 0) {
        screen_clear();
        print_centered(10, "랭킹 데이터가 없습니다.");
        print_centered(12, "Enter를 누르면 돌아갑니다.");
        int ch; do { ch = getchar(); } while (ch != '\n' && ch != EOF);
        free(arr);
        return;
    }

    qsort(arr, n, sizeof(RankRow), cmp_desc);

    // ===== 중앙정렬 출력 시작 =====
    screen_clear();
    cursor_hide();

    int termR, termC;
    get_terminal_size(&termR, &termC);

    print_centered(4, "========== TOP 10 RANKINGS ==========");

    int max_show = (n < 10 ? n : 10);
    int start_row = 7;

    for (int i = 0; i < max_show; i++) {
        char buf[200];
        sprintf(buf, "%2d) %-12s   %6d점   (%s)",
            i + 1, arr[i].name, arr[i].score, arr[i].when);
        print_centered(start_row + i, buf);
    }

    print_centered(start_row + max_show + 2, "========================================");
    print_centered(start_row + max_show + 4, "Enter를 누르면 계속합니다...");

    cursor_show();

    // 엔터 대기
    int ch;
    do { ch = getchar(); } while (ch != '\n' && ch != EOF);

    free(arr);
}


// ===== 메인 =====
int main(void) {
    srand((unsigned)time(NULL));
    enable_vt_mode(); // 윈도우 ANSI 켜기

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* p = strrchr(path, '\\');
    if (p) *(p + 1) = '\0';
    strcat(path, "bgm.wav");
    
    PlaySoundA(path, NULL, SND_FILENAME | SND_ASYNC | SND_LOOP);
    
    int board[ROWS][COLS];
    int spins = START_SPINS;
    int score = 0;
    int weights[10]; // 1~9 사용, 0은 미사용
    for (int n = 0; n < 10; n++) weights[n] = WEIGHT_BASE;
    for (int n = 5; n <= 9; n++) weights[n] += 1; // 시작부터 살짝 우대

    int num444Protect = 0;

    // ★★★ 시작 화면 중앙 정렬 + Enter 대기 ★★★
    screen_clear();
    cursor_hide();

    print_centered(5, "======== 3x4 슬롯머신 ========");
    print_centered(8, "+---------------------+");
    print_centered(9, "|     WELCOME  TO     |");
    print_centered(10, "|      SLOT GAME      |");
    print_centered(11, "+---------------------+");
    print_centered(14, "- 라인 점수: (숫자 × 라인길이)");
    print_centered(15, "- 444: 해당 라운드 0점획득");
    print_centered(16, "- 777: 라인 k개면 2^k 배율 적용");
    print_centered(19, "(q: 종료)");

    // 터미널 가로 크기 얻어서 "시작하려면..." 문자열의 시작열을 계산
    int termR, termC;
    get_terminal_size(&termR, &termC);

    int x = (termC - (int)strlen("시작하려면 Enter를 누르세요")) / 2;
    if (x < 1) x = 1;

    // 깜빡이는 출력 (행:18, 열:x)
    gotoxyflash_hit_lines(18, x);

    cursor_show();
    int ch = getchar();
    if (ch == 'q' || ch == 'Q') return 0;
    if (ch != '\n') while (getchar() != '\n'); // 나머지 버퍼 비우기

    screen_clear();

    while (spins > 0) {
        char info[100];
        sprintf(info, "[ 남은 스핀 %d | 점수 %d ]", spins, score);
        print_centered(3, info);     // 화면 3번째 줄(원하면 조정 가능)
        wait_enter();

        int peek = getchar();
        if (peek == 's' || peek == 'S') {
            shop_menu(&score, &spins, weights, &num444Protect);
            continue;
        }
        else if (peek == 'q' || peek == 'Q') {
            puts("게임을 종료합니다.");
            break;
        }
        else if (peek != '\n' && peek != EOF) {
            while (peek != '\n' && peek != EOF) peek = getchar();
        }

        spins--;

        // 보드 생성 + 스핀 애니메이션
        generate_board(board, weights);
        sfx_spin_start();
        spin_animation_frames(board, weights, SPIN_FRAMES, SPIN_DELAY_MS);

        flash_hit_lines(board, /*times=*/2, /*delay_ms=*/120);
        int termR, termC;
        get_terminal_size(&termR, &termC);
        int BW = board_text_width();
        int BH = board_text_height();
        int top = (termR - BH) / 2;
        if (top < 1) top = 1;
        g_msg_row = top + BH + 2;

        // 채점
        int c777 = 0, c444 = 0;
        int gained = score_lines(board, /*verbose=*/1, &c777, &c444);
        int round_base = gained;

        // 444 처리
        if (c444 > 0) {
            if (num444Protect) {
                int add444 = 12 * c444;
                round_base += add444;
                num444Protect--;
                {
                    int termR, termC;
                    get_terminal_size(&termR, &termC);

                    char msg[128];
                    sprintf(msg, "444 보호 부적 발동! 남은 부적 %d개 → +%d 점", num444Protect, add444);

                    int len = strlen(msg);
                    int row = termR / 2;       // 화면 중앙
                    int col = (termC - len) / 2;
                    if (col < 1) col = 1;

                    screen_clear();
                    gotoxy(row, col);
                    printf(ANSI_CYAN "%s" ANSI_RESET, msg);

                    const char* pressMsg = "Enter를 누르면 계속";
                    int col2 = (termC - strlen(pressMsg)) / 2;
                    if (col2 < 1) col2 = 1;
                    gotoxy(row + 2, col2);
                    printf("%s", pressMsg);

                    int ch;
                    do { ch = getchar(); } while (ch != '\n' && ch != EOF);
                }

            }
            else {
                {
                    int termR, termC;
                    get_terminal_size(&termR, &termC);

                    const char* msg = "!!! 444 발생: 이 라운드 점수 0 !!!";
                    int len = strlen(msg);
                    int row = termR / 2;
                    int col = (termC - len) / 2;
                    if (col < 1) col = 1;

                    screen_clear();
                    gotoxy(row, col);
                    printf(ANSI_RED "%s" ANSI_RESET, msg);

                    gotoxy(row + 2, (termC - 24) / 2);
                    printf("Enter를 누르면 계속");

                    int ch;
                    do { ch = getchar(); } while (ch != '\n' && ch != EOF);
                }

            }
        }

        // 777 배율
        int multiplier = (c777 > 0) ? (1 << c777) : 1;
        if (c777 > 0) {
            {
                int termR, termC;
                get_terminal_size(&termR, &termC);

                char msg[80];
                sprintf(msg, "!!! 777 %d개 → 배율 x%d !!!", c777, multiplier);

                int len = strlen(msg);
                int row = termR / 2;
                int col = (termC - len) / 2;
                if (col < 1) col = 1;

                screen_clear();
                gotoxy(row, col);
                printf(ANSI_YELLOW "%s" ANSI_RESET, msg);

                gotoxy(row + 2, (termC - 24) / 2);
                printf("Enter를 누르면 계속");

                int ch;
                do { ch = getchar(); } while (ch != '\n' && ch != EOF);
            }

        }

        int round_total = round_base * multiplier;
        score += round_total;

        // 연출
        if (round_total > 0) sfx_line_win();
        if (c777 > 0) sfx_big_win();
        flash_hit_lines(board, /*times=*/2, /*delay_ms=*/120);

        char msg1[100], msg2[120];
        sprintf(msg1, "라인 점수 합계: %+d", gained);
        sprintf(msg2, "이 라운드 획득 점수: %d (배율 적용 후) → 현재 점수 %d", round_total, score);
        print_centered(20, msg1);
        print_centered(21, msg2);

        if (spins == 0) {      // 이 라운드 끝나고 남은 스핀이 0이면 마지막 판
            wait_result_enter();
        }
    }

    // ===== 랭킹 저장/표시 (중앙 정렬 버전) =====
    {
        screen_clear();
        int termR, termC;
        get_terminal_size(&termR, &termC);

        char name[64];

        // 중앙 안내 텍스트
        print_centered(termR / 2 - 3, "게임 종료!");
        print_centered(termR / 2 - 1, "이름을 입력하세요 (엔터=Player)");

        // 이름 입력 위치 중앙에 커서 놓기
        gotoxy(termR / 2, termC / 2 - 8);
        if (fgets(name, sizeof(name), stdin) == NULL || name[0] == '\n') {
            strcpy(name, "Player");
        }
        else {
            char* p = strchr(name, '\n');
            if (p) *p = '\0';
        }

        char buf[80];
        sprintf(buf, "최종 점수: %d", score);
        print_centered(termR / 2 + 2, buf);

        print_centered(termR / 2 + 4, "Enter를 누르면 랭킹을 표시합니다.");

        int ch;
        do { ch = getchar(); } while (ch != '\n' && ch != EOF);

        // 랭킹 저장
        save_ranking(name, score);
    }

    // ===== TOP10 중앙정렬 출력 =====
    show_top10();

    // ===== 마지막 인사 화면 =====
    {
        PlaySound(NULL, 0, 0);

        screen_clear();
        int termR, termC;
        get_terminal_size(&termR, &termC);

        print_centered(termR / 2, "플레이해줘서 고마워!");
        print_centered(termR / 2 + 2, "Enter를 누르면 종료합니다.");

        int ch;
        do { ch = getchar(); } while (ch != '\n' && ch != EOF);
    }

    return 0;


}

