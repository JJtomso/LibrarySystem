#include <microhttpd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8888
#define MAX_BOOKS 10

// 图书结构体（共享资源）
typedef struct {
    char book_id[50];
    char name[100];
    int is_borrowed;          // 0: 在馆, 1: 已借出
    pthread_mutex_t lock;     // 每本书的互斥锁
} Book;

Book library[MAX_BOOKS];
int book_count = 0;

// 全局统计（用于并发测试）
int success_count = 0;
int fail_count = 0;
pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

// 初始化图书馆
void init_library() {
    strcpy(library[0].book_id, "B001");
    strcpy(library[0].name, "操作系统概念");
    library[0].is_borrowed = 0;
    pthread_mutex_init(&library[0].lock, NULL);
    book_count = 1;
}

// 线程参数结构体
typedef struct {
    int reader_id;
    Book *book;
} ThreadArg;

// 借书任务（线程函数）
void *borrow_task(void *arg) {
    ThreadArg *targ = (ThreadArg *)arg;
    int reader_id = targ->reader_id;
    Book *book = targ->book;

    // 尝试锁定这本书
    pthread_mutex_lock(&book->lock);
    if (!book->is_borrowed) {
        printf("[读者%d] 成功借走《%s》\n", reader_id, book->name);
        book->is_borrowed = 1;
        pthread_mutex_lock(&global_lock);
        success_count++;
        pthread_mutex_unlock(&global_lock);
    } else {
        printf("[读者%d] 发现《%s》已被借走\n", reader_id, book->name);
        pthread_mutex_lock(&global_lock);
        fail_count++;
        pthread_mutex_unlock(&global_lock);
    }
    pthread_mutex_unlock(&book->lock);
    return NULL;
}

// HTTP 请求处理回调
static enum MHD_Result answer_to_connection(void *cls,
                                            struct MHD_Connection *connection,
                                            const char *url,
                                            const char *method,
                                            const char *version,
                                            const char *upload_data,
                                            size_t *upload_data_size,
                                            void **con_cls) {
    // 消除未使用参数的警告
    (void)cls;
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    (void)con_cls;

    // 处理 OPTIONS 请求（CORS 预检）
    if (strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *response =
            MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods",
                                "GET, POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers",
                                "Content-Type");
        enum MHD_Result ret =
            MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    // 处理 POST /simulate-borrow
    if (strcmp(method, "POST") == 0 &&
        strcmp(url, "/simulate-borrow") == 0) {
        success_count = 0;
        fail_count = 0;

        pthread_t threads[5];
        ThreadArg args[5];

        // 创建5个线程并发抢书
        for (int i = 0; i < 5; i++) {
            args[i].reader_id = i + 1;
            args[i].book = &library[0];
            pthread_create(&threads[i], NULL, borrow_task, &args[i]);
        }
        for (int i = 0; i < 5; i++) {
            pthread_join(threads[i], NULL);
        }

        // 重置书本状态（方便多次测试）
        library[0].is_borrowed = 0;

        // 构造 JSON 响应
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"success_count\":%d,\"fail_count\":%d}", success_count,
                 fail_count);

        struct MHD_Response *response =
            MHD_create_response_from_buffer(strlen(json), (void *)json,
                                            MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        enum MHD_Result ret =
            MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    // 处理 GET /
    if (strcmp(method, "GET") == 0 && strcmp(url, "/") == 0) {
        const char *page =
            "{\"status\":\"running\",\"msg\":\"Backend is alive\"}";
        struct MHD_Response *response =
            MHD_create_response_from_buffer(strlen(page), (void *)page,
                                            MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        enum MHD_Result ret =
            MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    // 其他路径返回 404
    const char *not_found = "{\"error\":\"Not Found\"}";
    struct MHD_Response *response =
        MHD_create_response_from_buffer(strlen(not_found), (void *)not_found,
                                        MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    enum MHD_Result ret =
        MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}

int main() {
    init_library();

    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                              &answer_to_connection, NULL, MHD_OPTION_END);
    if (daemon == NULL) {
        fprintf(stderr, "❌ 无法启动服务器，端口 %d 可能被占用\n", PORT);
        return 1;
    }

    printf("✅ 后端服务器已启动，监听端口 %d...\n", PORT);
    printf("按回车键退出...\n");
    getchar();

    MHD_stop_daemon(daemon);
    return 0;
}