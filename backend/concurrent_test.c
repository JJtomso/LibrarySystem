/**
 * 图书馆系统并发测试程序（预期动态化）
 * 模拟多用户同时操作：借书、增减库存、删除图书
 * 不预设固定成功/失败，而是根据响应判断操作是否合法
 * 使用 libcurl + pthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <time.h>

#define API_BASE "http://localhost:8888"
#define TIMEOUT_MS 3000L

/* ---------- 工具：内存中接收响应 ---------- */
typedef struct {
    char *data;
    size_t len;
} ResponseBuf;

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    ResponseBuf *buf = (ResponseBuf*)userdata;
    size_t total = size * nmemb;
    char *new_data = realloc(buf->data, buf->len + total + 1);
    if (!new_data) return 0;
    buf->data = new_data;
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

/* ---------- 发送 HTTP 请求 ---------- */
static char* send_request(const char *url, const char *method, const char *body,
                          long *out_status) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    ResponseBuf resp = { .data = malloc(1), .len = 0 };
    resp.data[0] = '\0';

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, TIMEOUT_MS);
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (out_status) *out_status = http_code;

    char *result = NULL;
    if (res == CURLE_OK) {
        result = strdup(resp.data);
    } else {
        result = NULL;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    return result;
}

/* ---------- 简化的 POST/GET/DELETE 包装 ---------- */
static char* post(const char *endpoint, const char *body, long *status) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s", API_BASE, endpoint);
    return send_request(url, "POST", body, status);
}

static char* delete_req(const char *endpoint, const char *body, long *status) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s", API_BASE, endpoint);
    return send_request(url, "DELETE", body, status);
}

/* ---------- 准备阶段：注册读者、添加图书 ---------- */
static void prepare_data() {
    printf("\n=== 准备测试数据 ===\n");

    // 注册测试读者
    const char *readers[][3] = {
        {"TEST001", "测试学生1", "男"},
        {"TEST002", "测试学生2", "女"}
    };
    for (int i = 0; i < 2; i++) {
        char body[128];
        snprintf(body, sizeof(body),
                 "{\"id\":\"%s\",\"name\":\"%s\",\"gender\":\"%s\"}",
                 readers[i][0], readers[i][1], readers[i][2]);
        long status;
        char *resp = post("/register", body, &status);
        if (resp) {
            if (status == 200) {
                printf("✅ 注册读者 %s 成功\n", readers[i][0]);
            } else {
                printf("ℹ️  读者 %s 可能已存在 (status=%ld)\n", readers[i][0], status);
            }
            free(resp);
        } else {
            printf("❌ 注册读者 %s 失败\n", readers[i][0]);
        }
    }

    // 添加测试图书（若已存在则增加库存）
    typedef struct { char id[20]; char name[40]; int qty; } BookDef;
    BookDef books[] = {
        {"BOOKA", "并发测试书A", 2},
        {"BOOKB", "并发测试书B", 2},
        {"BOOKC", "并发测试书C", 1},
        {"BOOKD", "并发测试书D", 1},
        {"BOOKE", "并发测试书E", 1},
        {"BOOKF", "并发测试书F", 1}
    };
    for (int i = 0; i < 6; i++) {
        char body[256];
        snprintf(body, sizeof(body),
                 "{\"id\":\"%s\",\"name\":\"%s\",\"author\":\"测试\","
                 "\"publisher\":\"测试出版社\",\"price\":10.00,\"location\":\"测试区\"}",
                 books[i].id, books[i].name);
        long status;
        char *resp = post("/add-book", body, &status);
        if (resp) {
            if (status == 200) {
                printf("✅ 添加图书 %s 成功 (或增加库存)\n", books[i].id);
            } else {
                printf("⚠️  添加图书 %s 异常 status=%ld\n", books[i].id, status);
            }
            free(resp);
        } else {
            printf("❌ 添加图书 %s 失败\n", books[i].id);
        }
    }

    printf("ℹ️  已准备图书，库存请自行确认（通常足够）\n");
    printf("=== 准备完成 ===\n\n");
}

/* ---------- 每个任务的合法性检查函数 ---------- */
typedef int (*CheckFunc)(long status, const char *response, char *reason, size_t reason_sz);

/* 借书检查：允许成功(200) 或 因库存不足/已借出等失败(400) */
static int check_borrow(long status, const char *resp, char *reason, size_t sz) {
    if (status == 200) {
        if (strstr(resp, "\"status\":\"ok\"")) {
            snprintf(reason, sz, "借书成功");
            return 1;
        }
    } else if (status == 400) {
        if (strstr(resp, "no available copy") || 
            strstr(resp, "already borrowed") ||
            strstr(resp, "book not found") ||
            strstr(resp, "reader not found")) {
            snprintf(reason, sz, "借书失败（合法原因：%s）", resp);
            return 1;
        }
    }
    snprintf(reason, sz, "非预期的状态码/响应: status=%ld, resp=%s", status, resp);
    return 0;
}

/* 调整库存检查：允许成功(200) 或 因库存不足等失败(400) */
static int check_update_quantity(long status, const char *resp, char *reason, size_t sz) {
    if (status == 200) {
        if (strstr(resp, "\"status\":\"ok\"")) {
            snprintf(reason, sz, "调整库存成功");
            return 1;
        }
    } else if (status == 400) {
        if (strstr(resp, "Invalid quantity") || strstr(resp, "Book not found")) {
            snprintf(reason, sz, "调整库存失败（合法原因：%s）", resp);
            return 1;
        }
    }
    snprintf(reason, sz, "非预期的状态码/响应: status=%ld, resp=%s", status, resp);
    return 0;
}

/* 删除图书检查：允许成功(200) 或 因有未还副本等失败(400) */
static int check_delete_book(long status, const char *resp, char *reason, size_t sz) {
    if (status == 200) {
        if (strstr(resp, "\"status\":\"ok\"")) {
            snprintf(reason, sz, "删除图书成功");
            return 1;
        }
    } else if (status == 400) {
        if (strstr(resp, "Cannot delete book with borrowed copies") || 
            strstr(resp, "Book not found")) {
            snprintf(reason, sz, "删除图书失败（合法原因：%s）", resp);
            return 1;
        }
    }
    snprintf(reason, sz, "非预期的状态码/响应: status=%ld, resp=%s", status, resp);
    return 0;
}

/* ---------- 任务定义 ---------- */
typedef struct {
    int id;
    char *description;
    char *method;          // "POST" 或 "DELETE"
    char *endpoint;
    char *body;
    CheckFunc check;       // 合法性检查函数
} Task;

/* 线程函数参数 */
typedef struct {
    Task task;
    pthread_barrier_t *barrier;
    int thread_id;
} ThreadArg;

/* ---------- 线程函数 ---------- */
static void* thread_func(void *arg) {
    ThreadArg *targ = (ThreadArg*)arg;
    Task *task = &targ->task;
    pthread_barrier_t *barrier = targ->barrier;

    pthread_barrier_wait(barrier);

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_REALTIME, &ts_start);

    long status = 0;
    char *resp = NULL;
    if (strcmp(task->method, "POST") == 0) {
        resp = post(task->endpoint, task->body, &status);
    } else if (strcmp(task->method, "DELETE") == 0) {
        resp = delete_req(task->endpoint, task->body, &status);
    } else {
        fprintf(stderr, "未知方法 %s\n", task->method);
        pthread_exit(NULL);
    }

    clock_gettime(CLOCK_REALTIME, &ts_end);
    double elapsed = (ts_end.tv_sec - ts_start.tv_sec) +
                     (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;

    // 检查合法性
    char reason[512] = {0};
    int ok = task->check(status, resp ? resp : "", reason, sizeof(reason));

    char timebuf[64];
    struct tm *tm_info = localtime(&ts_start.tv_sec);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    printf("[%s.%03ld] [T%d] %s\n",
           timebuf, ts_start.tv_nsec / 1000000,
           targ->thread_id, task->description);
    printf("          → %s %s (status=%ld, %.3fs) ",
           task->method, task->endpoint, status, elapsed);
    if (ok) {
        printf("✅ %s\n", reason);
    } else {
        printf("❌ %s\n", reason);
    }
    if (resp) {
        char resp_short[201];
        strncpy(resp_short, resp, 200);
        resp_short[200] = '\0';
        printf("          └─ 响应: %s\n", resp_short);
        free(resp);
    }

    pthread_exit(NULL);
}

/* ---------- 主函数 ---------- */
int main(void) {
    curl_global_init(CURL_GLOBAL_ALL);
    prepare_data();

    // 定义并发任务（不再预设成功/失败，只检查合法性）
    #define NUM_TASKS 10
    Task tasks[NUM_TASKS] = {
        {1, "借书 BOOKA 由 TEST001", "POST", "/borrow",
         "{\"reader_id\":\"TEST001\",\"book_id\":\"BOOKA\",\"days\":30}", check_borrow},
        {2, "借书 BOOKB 由 TEST002", "POST", "/borrow",
         "{\"reader_id\":\"TEST002\",\"book_id\":\"BOOKB\",\"days\":30}", check_borrow},
        {3, "借书 BOOKC 由 TEST001 (竞争)", "POST", "/borrow",
         "{\"reader_id\":\"TEST001\",\"book_id\":\"BOOKC\",\"days\":30}", check_borrow},
        {4, "借书 BOOKC 由 TEST002 (竞争)", "POST", "/borrow",
         "{\"reader_id\":\"TEST002\",\"book_id\":\"BOOKC\",\"days\":30}", check_borrow},
        {5, "借书 BOOKD 由 TEST001", "POST", "/borrow",
         "{\"reader_id\":\"TEST001\",\"book_id\":\"BOOKD\",\"days\":30}", check_borrow},
        {6, "管理员增加 BOOKD 库存 +2", "POST", "/update-quantity",
         "{\"book_id\":\"BOOKD\",\"delta\":2}", check_update_quantity},
        {7, "管理员减少 BOOKE 库存 -1", "POST", "/update-quantity",
         "{\"book_id\":\"BOOKE\",\"delta\":-1}", check_update_quantity},
        {8, "借书 BOOKE 由 TEST002", "POST", "/borrow",
         "{\"reader_id\":\"TEST002\",\"book_id\":\"BOOKE\",\"days\":30}", check_borrow},
        {9, "管理员删除 BOOKF", "DELETE", "/remove-book",
         "{\"id\":\"BOOKF\"}", check_delete_book},
        {10, "借书 BOOKF 由 TEST001", "POST", "/borrow",
         "{\"reader_id\":\"TEST001\",\"book_id\":\"BOOKF\",\"days\":30}", check_borrow}
    };

    pthread_t threads[NUM_TASKS];
    ThreadArg args[NUM_TASKS];
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, NUM_TASKS);

    printf("\n=== 开始并发测试 (%d 个任务) ===\n", NUM_TASKS);
    for (int i = 0; i < NUM_TASKS; i++) {
        args[i].task = tasks[i];
        args[i].barrier = &barrier;
        args[i].thread_id = i + 1;
        pthread_create(&threads[i], NULL, thread_func, &args[i]);
    }

    for (int i = 0; i < NUM_TASKS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_barrier_destroy(&barrier);
    printf("\n=== 并发测试结束 ===\n");

    curl_global_cleanup();
    return 0;
}