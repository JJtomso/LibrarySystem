/**
 * 图书馆管理系统 - 后端服务
 * 基于 libmicrohttpd，提供 RESTful API
 * 数据持久化至二进制文件 library.dat
 */

#include <microhttpd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>   // for fsync
#include <stdarg.h>   // for va_list

/* ==================== 常量配置 ==================== */
#define PORT                8888
#define MAX_READERS         1000
#define MAX_BOOKS           1000
#define MAX_RECORDS         5000
#define DATA_FILE           "library.dat"
#define BUFFER_SIZE         32768   // 增大缓冲区
#define DATE_STR_LEN        20

/* ==================== 数据结构 ==================== */
typedef struct {
    char id[10];        // 9位学号
    char name[50];
    char gender[4];     // "男" / "女"
} Reader;

typedef struct {
    char book_id[20];
    char name[100];
    char author[50];
    char publisher[50];
    double price;
    char location[50];
    int quantity;       // 总数量
    int borrowed;       // 已借出数量
} Book;

typedef struct {
    char reader_id[10];
    char book_id[20];
    char borrow_date[DATE_STR_LEN];
    char due_date[DATE_STR_LEN];
    char return_date[DATE_STR_LEN];  // 空串表示未还
} BorrowRecord;

/* ==================== 全局数据 ==================== */
static Reader readers[MAX_READERS];
static int reader_count = 0;

static Book books[MAX_BOOKS];
static int book_count = 0;

static BorrowRecord records[MAX_RECORDS];
static int record_count = 0;

static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ==================== 日志工具 ==================== */
static void log_info(const char *fmt, ...) {
    va_list args;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
    printf("[%s] ", timebuf);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

#define LOG_INFO(...) log_info(__VA_ARGS__)
#define LOG_ERROR(...) log_info("[ERROR] " __VA_ARGS__)

/* ==================== 数据持久化 ==================== */
void save_data(void) {
    FILE *fp = fopen(DATA_FILE, "wb");
    if (!fp) {
        LOG_ERROR("无法打开数据文件 %s 进行写入", DATA_FILE);
        return;
    }

    size_t written = 0;
    written += fwrite(&reader_count, sizeof(int), 1, fp);
    written += fwrite(&book_count, sizeof(int), 1, fp);
    written += fwrite(&record_count, sizeof(int), 1, fp);

    if (reader_count > 0)
        written += fwrite(readers, sizeof(Reader), reader_count, fp);
    if (book_count > 0)
        written += fwrite(books, sizeof(Book), book_count, fp);
    if (record_count > 0)
        written += fwrite(records, sizeof(BorrowRecord), record_count, fp);

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    LOG_INFO("数据已保存 (读者:%d, 图书:%d, 记录:%d)", 
             reader_count, book_count, record_count);
}

void load_data(void) {
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) {
        LOG_INFO("数据文件不存在，初始化空数据");
        reader_count = 0;
        book_count = 0;
        record_count = 0;
        return;
    }

    fread(&reader_count, sizeof(int), 1, fp);
    fread(&book_count, sizeof(int), 1, fp);
    fread(&record_count, sizeof(int), 1, fp);

    if (reader_count > 0 && reader_count <= MAX_READERS)
        fread(readers, sizeof(Reader), reader_count, fp);
    if (book_count > 0 && book_count <= MAX_BOOKS)
        fread(books, sizeof(Book), book_count, fp);
    if (record_count > 0 && record_count <= MAX_RECORDS)
        fread(records, sizeof(BorrowRecord), record_count, fp);

    fclose(fp);
    LOG_INFO("加载数据成功 (读者:%d, 图书:%d, 记录:%d)",
             reader_count, book_count, record_count);
}

/* ==================== 初始化样例数据 ==================== */
void init_sample_data(void) {
    if (reader_count > 0 || book_count > 0) {
        LOG_INFO("已有数据，跳过初始化");
        return;
    }

    // 读者
    strcpy(readers[0].id, "202100101");
    strcpy(readers[0].name, "张三");
    strcpy(readers[0].gender, "男");
    reader_count = 1;

    // 图书
    strcpy(books[0].book_id, "TP312C");
    strcpy(books[0].name, "C程序设计");
    strcpy(books[0].author, "谭浩强");
    strcpy(books[0].publisher, "清华大学出版社");
    books[0].price = 39.00;
    strcpy(books[0].location, "A区3排");
    books[0].quantity = 5;
    books[0].borrowed = 0;
    book_count = 1;

    // 借阅记录（示例逾期）
    strcpy(records[0].reader_id, "202100101");
    strcpy(records[0].book_id, "TP312C");
    strcpy(records[0].borrow_date, "2026-05-01");
    strcpy(records[0].due_date, "2026-06-01");
    records[0].return_date[0] = '\0';
    record_count = 1;

    save_data();
    LOG_INFO("初始化样例数据完成");
}

/* ==================== 工具函数 ==================== */

static void today_str(char *buf, size_t sz) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, sz, "%Y-%m-%d", tm);
}

static void future_date(int days, char *buf, size_t sz) {
    time_t t = time(NULL) + days * 86400L;
    struct tm *tm = localtime(&t);
    strftime(buf, sz, "%Y-%m-%d", tm);
}

// 返回静态缓冲区，调用后需立即复制
static char* json_get_string(const char *json, const char *key) {
    static char val[256];
    val[0] = '\0';
    if (!json || !key) return val;

    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        snprintf(pat, sizeof(pat), "\"%s\" : \"", key);
        p = strstr(json, pat);
    }
    if (!p) {
        LOG_INFO("[JSON_DEBUG] Key '%s' not found!", key);
        return val;
    }

    p += strlen(pat);
    while (*p == ' ') p++;

    const char *end = strchr(p, '"');
    if (!end) {
        LOG_INFO("[JSON_DEBUG] No closing quote for key '%s'", key);
        return val;
    }

    size_t len = end - p;
    if (len >= sizeof(val)) len = sizeof(val) - 1;
    memcpy(val, p, len);
    val[len] = '\0';

    return val;
}

static double json_get_double(const char *json, const char *key) {
    char *s = json_get_string(json, key);
    return s[0] ? atof(s) : 0.0;
}

/* ==================== 业务逻辑函数 ==================== */

static int register_reader_internal(const char *id, const char *name, const char *gender) {
    for (int i = 0; i < reader_count; i++) {
        if (strcmp(readers[i].id, id) == 0)
            return -1;  // 已存在
    }
    if (reader_count >= MAX_READERS)
        return -2;  // 空间满

    strncpy(readers[reader_count].id, id, sizeof(readers[0].id) - 1);
    readers[reader_count].id[sizeof(readers[0].id) - 1] = '\0';
    strncpy(readers[reader_count].name, name, sizeof(readers[0].name) - 1);
    readers[reader_count].name[sizeof(readers[0].name) - 1] = '\0';
    strncpy(readers[reader_count].gender, gender, sizeof(readers[0].gender) - 1);
    readers[reader_count].gender[sizeof(readers[0].gender) - 1] = '\0';
    reader_count++;
    return 0;
}

static int borrow_book_internal(const char *reader_id, const char *book_id, int days, char *out_due_date) {
    int reader_ok = 0;
    for (int i = 0; i < reader_count; i++) {
        if (strcmp(readers[i].id, reader_id) == 0) { reader_ok = 1; break; }
    }
    if (!reader_ok) return -1;

    int book_idx = -1;
    for (int i = 0; i < book_count; i++) {
        if (strcmp(books[i].book_id, book_id) == 0) { book_idx = i; break; }
    }
    if (book_idx < 0) return -2;
    if (books[book_idx].borrowed >= books[book_idx].quantity) return -3;
    if (record_count >= MAX_RECORDS) return -4;

    BorrowRecord *rec = &records[record_count];
    strncpy(rec->reader_id, reader_id, sizeof(rec->reader_id) - 1);
    rec->reader_id[sizeof(rec->reader_id) - 1] = '\0';
    strncpy(rec->book_id, book_id, sizeof(rec->book_id) - 1);
    rec->book_id[sizeof(rec->book_id) - 1] = '\0';
    today_str(rec->borrow_date, sizeof(rec->borrow_date));
    future_date(days, rec->due_date, sizeof(rec->due_date));
    rec->return_date[0] = '\0';
    record_count++;

    books[book_idx].borrowed++;

    if (out_due_date) strcpy(out_due_date, rec->due_date);
    return 0;
}

/* ==================== HTTP 请求处理 ==================== */

typedef struct {
    char buf[BUFFER_SIZE];
    size_t len;
} ReqBuf;

static enum MHD_Result send_response(struct MHD_Connection *connection,
                                     int status_code,
                                     const char *body) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(
        strlen(body), (void*)body, MHD_RESPMEM_MUST_COPY);
    if (!resp) return MHD_NO;
    MHD_add_response_header(resp, "Content-Type", "application/json");
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
    enum MHD_Result ret = MHD_queue_response(connection, status_code, resp);
    MHD_destroy_response(resp);
    return ret;
}

/* ----- GET /readers ----- */
static enum MHD_Result handle_get_readers(struct MHD_Connection *connection) {
    pthread_mutex_lock(&data_mutex);
    char tmp[BUFFER_SIZE];
    int pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "[");
    for (int i = 0; i < reader_count; i++) {
        if (i > 0) pos += snprintf(tmp + pos, sizeof(tmp) - pos, ",");
        pos += snprintf(tmp + pos, sizeof(tmp) - pos,
            "{\"id\":\"%s\",\"name\":\"%s\",\"gender\":\"%s\"}",
            readers[i].id, readers[i].name, readers[i].gender);
    }
    snprintf(tmp + pos, sizeof(tmp) - pos, "]");
    pthread_mutex_unlock(&data_mutex);
    return send_response(connection, MHD_HTTP_OK, tmp);
}

/* ----- POST /register ----- */
static enum MHD_Result handle_register(struct MHD_Connection *connection,
                                       const char *body) {
    if (!body || !*body) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Empty request body\"}");
    }

    char id[10] = {0};
    char name[50] = {0};
    char gender[4] = {0};

    char *p = json_get_string(body, "id");
    strncpy(id, p, sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';

    p = json_get_string(body, "name");
    strncpy(name, p, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    p = json_get_string(body, "gender");
    strncpy(gender, p, sizeof(gender) - 1);
    gender[sizeof(gender) - 1] = '\0';

    if (!id[0] || !name[0] || !gender[0]) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Missing id, name or gender\"}");
    }
    if (strlen(id) > 9) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"ID too long (max 9)\"}");
    }

    pthread_mutex_lock(&data_mutex);
    int ret = register_reader_internal(id, name, gender);
    if (ret == 0) {
        save_data();
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("新读者注册成功: id=%s, name=%s", id, name);
        return send_response(connection, MHD_HTTP_OK,
                             "{\"status\":\"ok\",\"msg\":\"Registered successfully\"}");
    } else {
        pthread_mutex_unlock(&data_mutex);
        if (ret == -1) {
            LOG_INFO("注册失败: Reader already exists (id=%s)", id);
            return send_response(connection, MHD_HTTP_BAD_REQUEST,
                                 "{\"error\":\"Reader already exists\",\"code\":\"ALREADY_EXISTS\"}");
        } else {
            LOG_INFO("注册失败: Reader list full");
            return send_response(connection, MHD_HTTP_BAD_REQUEST,
                                 "{\"error\":\"Reader list full\"}");
        }
    }
}

/* ----- GET /books ----- */
static enum MHD_Result handle_get_books(struct MHD_Connection *connection) {
    pthread_mutex_lock(&data_mutex);
    char tmp[BUFFER_SIZE];
    int pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "[");
    for (int i = 0; i < book_count; i++) {
        if (i > 0) pos += snprintf(tmp + pos, sizeof(tmp) - pos, ",");
        int available = books[i].quantity - books[i].borrowed;
        char status[32];
        if (available > 0)
            snprintf(status, sizeof(status), "馆藏(余%d)", available);
        else
            snprintf(status, sizeof(status), "已借出");

        pos += snprintf(tmp + pos, sizeof(tmp) - pos,
            "{\"id\":\"%s\",\"name\":\"%s\",\"author\":\"%s\",\"publisher\":\"%s\","
            "\"price\":%.2f,\"location\":\"%s\",\"quantity\":%d,\"borrowed\":%d,\"status\":\"%s\"}",
            books[i].book_id, books[i].name, books[i].author, books[i].publisher,
            books[i].price, books[i].location, books[i].quantity, books[i].borrowed, status);
    }
    snprintf(tmp + pos, sizeof(tmp) - pos, "]");
    pthread_mutex_unlock(&data_mutex);
    return send_response(connection, MHD_HTTP_OK, tmp);
}

/* ----- POST /add-book ----- */
static enum MHD_Result handle_add_book(struct MHD_Connection *connection,
                                       const char *body) {
    if (!body || !*body) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Empty body\"}");
    }

    char id[20] = {0};
    char name[100] = {0};
    char author[50] = {0};
    char publisher[50] = {0};
    char location[50] = {0};

    char *p = json_get_string(body, "id");
    strncpy(id, p, sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';

    p = json_get_string(body, "name");
    strncpy(name, p, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    p = json_get_string(body, "author");
    strncpy(author, p, sizeof(author) - 1);
    author[sizeof(author) - 1] = '\0';

    p = json_get_string(body, "publisher");
    strncpy(publisher, p, sizeof(publisher) - 1);
    publisher[sizeof(publisher) - 1] = '\0';

    // 尝试获取价格（支持字符串和数字两种格式）
    double price = json_get_double(body, "price");
    // 如果 price 为 0 且可能实际不是 0，尝试直接解析数字
    if (price == 0.0) {
        const char *ptr = strstr(body, "\"price\"");
        if (!ptr) ptr = strstr(body, "\"price\" :");
        if (ptr) {
            ptr = strchr(ptr, ':');
            if (ptr) {
                ptr++;
                while (*ptr == ' ' || *ptr == '\t') ptr++;
                if (*ptr >= '0' && *ptr <= '9') {
                    price = atof(ptr);
                }
            }
        }
    }

    p = json_get_string(body, "location");
    strncpy(location, p, sizeof(location) - 1);
    location[sizeof(location) - 1] = '\0';

    if (!id[0] || !name[0]) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Missing id or name\"}");
    }

    pthread_mutex_lock(&data_mutex);
    int found = -1;
    for (int i = 0; i < book_count; i++) {
        if (strcmp(books[i].book_id, id) == 0) { found = i; break; }
    }

    if (found >= 0) {
        books[found].quantity++;
        save_data();
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("图书数量增加: id=%s", id);
        return send_response(connection, MHD_HTTP_OK,
                             "{\"status\":\"ok\",\"msg\":\"quantity increased\"}");
    } else {
        if (book_count >= MAX_BOOKS) {
            pthread_mutex_unlock(&data_mutex);
            return send_response(connection, MHD_HTTP_BAD_REQUEST,
                                 "{\"error\":\"Book list full\"}");
        }
        strncpy(books[book_count].book_id, id, sizeof(books[0].book_id) - 1);
        books[book_count].book_id[sizeof(books[0].book_id) - 1] = '\0';
        strncpy(books[book_count].name, name, sizeof(books[0].name) - 1);
        books[book_count].name[sizeof(books[0].name) - 1] = '\0';
        strncpy(books[book_count].author, author, sizeof(books[0].author) - 1);
        books[book_count].author[sizeof(books[0].author) - 1] = '\0';
        strncpy(books[book_count].publisher, publisher, sizeof(books[0].publisher) - 1);
        books[book_count].publisher[sizeof(books[0].publisher) - 1] = '\0';
        books[book_count].price = price;
        strncpy(books[book_count].location, location, sizeof(books[0].location) - 1);
        books[book_count].location[sizeof(books[0].location) - 1] = '\0';
        books[book_count].quantity = 1;
        books[book_count].borrowed = 0;
        book_count++;
        save_data();
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("新增图书: id=%s, name=%s", id, name);
        return send_response(connection, MHD_HTTP_OK,
                             "{\"status\":\"ok\",\"msg\":\"new book added\"}");
    }
}

/* ----- DELETE /remove-book ----- */
static enum MHD_Result handle_remove_book(struct MHD_Connection *connection,
                                          const char *body) {
    if (!body || !*body) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Empty body\"}");
    }
    char *id = json_get_string(body, "id");
    if (!id[0]) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Missing id\"}");
    }

    pthread_mutex_lock(&data_mutex);
    int found = -1;
    for (int i = 0; i < book_count; i++) {
        if (strcmp(books[i].book_id, id) == 0) { found = i; break; }
    }
    if (found < 0) {
        pthread_mutex_unlock(&data_mutex);
        return send_response(connection, MHD_HTTP_NOT_FOUND,
                             "{\"error\":\"Book not found\"}");
    }

    if (books[found].borrowed > 0) {
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("删除图书失败: 尚有未还副本, id=%s", id);
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Cannot delete, book has borrowed copies\"}");
    }

    if (books[found].quantity > 1) {
        books[found].quantity--;
        save_data();
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("图书数量减少: id=%s", id);
        return send_response(connection, MHD_HTTP_OK,
                             "{\"status\":\"ok\",\"msg\":\"quantity decreased\"}");
    } else {
        for (int i = found; i < book_count - 1; i++) {
            books[i] = books[i + 1];
        }
        book_count--;
        save_data();
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("图书已删除: id=%s", id);
        return send_response(connection, MHD_HTTP_OK,
                             "{\"status\":\"ok\",\"msg\":\"book removed\"}");
    }
}

/* ----- POST /borrow ----- */
static enum MHD_Result handle_borrow(struct MHD_Connection *connection,
                                     const char *body) {
    if (!body || !*body) {
        LOG_INFO("[BORROW] Empty body");
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Empty body\"}");
    }

    char reader_id[10] = {0};
    char book_id[20] = {0};
    char days_str[10] = {0};

    char *tmp_rid = json_get_string(body, "reader_id");
    strncpy(reader_id, tmp_rid, sizeof(reader_id) - 1);
    reader_id[sizeof(reader_id) - 1] = '\0';

    char *tmp_bid = json_get_string(body, "book_id");
    strncpy(book_id, tmp_bid, sizeof(book_id) - 1);
    book_id[sizeof(book_id) - 1] = '\0';

    char *tmp_days = json_get_string(body, "days");
    strncpy(days_str, tmp_days, sizeof(days_str) - 1);
    days_str[sizeof(days_str) - 1] = '\0';

    if (!reader_id[0] || !book_id[0]) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Missing reader_id or book_id\"}");
    }

    int days = days_str[0] ? atoi(days_str) : 30;
    if (days <= 0) days = 30;

    pthread_mutex_lock(&data_mutex);
    for (int i = 0; i < record_count; i++) {
        if (strcmp(records[i].reader_id, reader_id) == 0 &&
            strcmp(records[i].book_id, book_id) == 0 &&
            records[i].return_date[0] == '\0') {
            pthread_mutex_unlock(&data_mutex);
            LOG_INFO("借书失败: 已借未还, reader=%s, book=%s", reader_id, book_id);
            return send_response(connection, MHD_HTTP_BAD_REQUEST,
                                 "{\"error\":\"You have already borrowed this book and not returned\"}");
        }
    }

    char due_date[DATE_STR_LEN];
    int ret = borrow_book_internal(reader_id, book_id, days, due_date);
    if (ret == 0) {
        save_data();
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("借书成功: reader=%s, book=%s, due=%s", reader_id, book_id, due_date);
        char resp[256];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"ok\",\"msg\":\"borrowed\",\"due_date\":\"%s\"}",
                 due_date);
        return send_response(connection, MHD_HTTP_OK, resp);
    } else {
        pthread_mutex_unlock(&data_mutex);
        const char *err_msg;
        switch (ret) {
            case -1: err_msg = "reader not found"; break;
            case -2: err_msg = "book not found"; break;
            case -3: err_msg = "no available copy"; break;
            case -4: err_msg = "record list full"; break;
            default: err_msg = "unknown error";
        }
        LOG_INFO("借书失败: %s", err_msg);
        char resp[256];
        snprintf(resp, sizeof(resp), "{\"error\":\"%s\"}", err_msg);
        int code = (ret == -1) ? MHD_HTTP_FORBIDDEN :
                   (ret == -2) ? MHD_HTTP_NOT_FOUND : MHD_HTTP_BAD_REQUEST;
        return send_response(connection, code, resp);
    }
}

/* ----- GET /myrecords (使用 MHD_get_connection_values 获取参数) ----- */

// 回调结构：用于在迭代中捕获 reader_id
typedef struct {
    char reader_id[10];
    int found;
} ParamContext;

static enum MHD_Result find_reader_id_cb(void *cls, enum MHD_ValueKind kind,
                                         const char *key, const char *value) {
    ParamContext *ctx = (ParamContext*)cls;
    if (strcmp(key, "reader_id") == 0) {
        strncpy(ctx->reader_id, value, sizeof(ctx->reader_id) - 1);
        ctx->reader_id[sizeof(ctx->reader_id) - 1] = '\0';
        ctx->found = 1;
        return MHD_NO;  // 停止迭代
    }
    return MHD_YES;
}

static enum MHD_Result handle_myrecords(struct MHD_Connection *connection,
                                        const char *url) {
    ParamContext ctx = { .reader_id = {0}, .found = 0 };
    MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND,
                              find_reader_id_cb, &ctx);

    LOG_INFO("[MYRECORDS] URL: %s, reader_id: %s, found: %d",
             url ? url : "(null)", ctx.reader_id, ctx.found);

    if (!ctx.found || ctx.reader_id[0] == '\0') {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"missing reader_id\"}");
    }

    pthread_mutex_lock(&data_mutex);
    char tmp[BUFFER_SIZE];
    int pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "[");
    int first = 1;
    char today[DATE_STR_LEN];
    today_str(today, sizeof(today));

    for (int r = 0; r < record_count; r++) {
        if (strcmp(records[r].reader_id, ctx.reader_id) != 0) continue;
        if (!first) pos += snprintf(tmp + pos, sizeof(tmp) - pos, ",");
        first = 0;

        int overdue_days = 0;
        if (records[r].return_date[0] == '\0') {
            if (strcmp(today, records[r].due_date) > 0) {
                int y1, m1, d1, y2, m2, d2;
                sscanf(today, "%d-%d-%d", &y1, &m1, &d1);
                sscanf(records[r].due_date, "%d-%d-%d", &y2, &m2, &d2);
                int days1 = y1 * 365 + m1 * 30 + d1;
                int days2 = y2 * 365 + m2 * 30 + d2;
                overdue_days = days1 - days2;
                if (overdue_days < 0) overdue_days = 0;
            }
        }

        pos += snprintf(tmp + pos, sizeof(tmp) - pos,
            "{\"book_id\":\"%s\",\"borrow_date\":\"%s\",\"due_date\":\"%s\","
            "\"return_date\":\"%s\",\"overdue_days\":%d}",
            records[r].book_id, records[r].borrow_date, records[r].due_date,
            records[r].return_date[0] ? records[r].return_date : "未还",
            overdue_days);
    }
    snprintf(tmp + pos, sizeof(tmp) - pos, "]");
    pthread_mutex_unlock(&data_mutex);
    LOG_INFO("[MYRECORDS] Response built, length %zu", strlen(tmp));
    return send_response(connection, MHD_HTTP_OK, tmp);
}

/* ----- GET /overdue ----- */
static enum MHD_Result handle_overdue(struct MHD_Connection *connection) {
    pthread_mutex_lock(&data_mutex);
    char tmp[BUFFER_SIZE];
    int pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "[");
    int first = 1;
    char today[DATE_STR_LEN];
    today_str(today, sizeof(today));

    for (int r = 0; r < record_count; r++) {
        if (records[r].return_date[0] != '\0') continue;
        if (strcmp(today, records[r].due_date) <= 0) continue;

        int y1, m1, d1, y2, m2, d2;
        sscanf(today, "%d-%d-%d", &y1, &m1, &d1);
        sscanf(records[r].due_date, "%d-%d-%d", &y2, &m2, &d2);
        int days1 = y1 * 365 + m1 * 30 + d1;
        int days2 = y2 * 365 + m2 * 30 + d2;
        int overdue = days1 - days2;
        if (overdue <= 0) continue;

        if (!first) pos += snprintf(tmp + pos, sizeof(tmp) - pos, ",");
        first = 0;

        char reader_name[50] = "未知";
        for (int i = 0; i < reader_count; i++) {
            if (strcmp(readers[i].id, records[r].reader_id) == 0) {
                strncpy(reader_name, readers[i].name, sizeof(reader_name) - 1);
                reader_name[sizeof(reader_name) - 1] = '\0';
                break;
            }
        }

        pos += snprintf(tmp + pos, sizeof(tmp) - pos,
            "{\"reader_id\":\"%s\",\"reader_name\":\"%s\",\"book_id\":\"%s\","
            "\"borrow_date\":\"%s\",\"due_date\":\"%s\",\"overdue_days\":%d}",
            records[r].reader_id, reader_name, records[r].book_id,
            records[r].borrow_date, records[r].due_date, overdue);
    }
    snprintf(tmp + pos, sizeof(tmp) - pos, "]");
    pthread_mutex_unlock(&data_mutex);
    return send_response(connection, MHD_HTTP_OK, tmp);
}

/* ----- GET /login ----- */
static enum MHD_Result handle_login(struct MHD_Connection *connection,
                                    const char *url) {
    ParamContext ctx = { .reader_id = {0}, .found = 0 };
    MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND,
                              find_reader_id_cb, &ctx);

    if (!ctx.found || ctx.reader_id[0] == '\0') {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"missing reader_id\"}");
    }

    pthread_mutex_lock(&data_mutex);
    int found = 0;
    for (int i = 0; i < reader_count; i++) {
        if (strcmp(readers[i].id, ctx.reader_id) == 0) {
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&data_mutex);

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"exists\":%s}", found ? "true" : "false");
    LOG_INFO("登录校验: reader_id=%s, exists=%d", ctx.reader_id, found);
    return send_response(connection, MHD_HTTP_OK, resp);
}

/* ----- POST /update-quantity (新增) ----- */
static enum MHD_Result handle_update_quantity(struct MHD_Connection *connection,
                                              const char *body) {
    if (!body || !*body) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Empty body\"}");
    }

    char book_id[20] = {0};
    char delta_str[12] = {0};

    char *p = json_get_string(body, "book_id");
    strncpy(book_id, p, sizeof(book_id) - 1);
    book_id[sizeof(book_id) - 1] = '\0';

    p = json_get_string(body, "delta");
    strncpy(delta_str, p, sizeof(delta_str) - 1);
    delta_str[sizeof(delta_str) - 1] = '\0';

    if (!book_id[0] || !delta_str[0]) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Missing book_id or delta\"}");
    }

    int delta = atoi(delta_str);
    if (delta == 0) {
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Delta must be non-zero\"}");
    }

    pthread_mutex_lock(&data_mutex);

    int idx = -1;
    for (int i = 0; i < book_count; i++) {
        if (strcmp(books[i].book_id, book_id) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        pthread_mutex_unlock(&data_mutex);
        return send_response(connection, MHD_HTTP_NOT_FOUND,
                             "{\"error\":\"Book not found\"}");
    }

    int new_qty = books[idx].quantity + delta;
    if (new_qty < 0) {
        pthread_mutex_unlock(&data_mutex);
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"Quantity cannot be negative\"}");
    }
    if (new_qty < books[idx].borrowed) {
        pthread_mutex_unlock(&data_mutex);
        return send_response(connection, MHD_HTTP_BAD_REQUEST,
                             "{\"error\":\"New quantity cannot be less than borrowed copies\"}");
    }

    books[idx].quantity = new_qty;
    save_data();
    pthread_mutex_unlock(&data_mutex);

    LOG_INFO("调整馆藏数量: book_id=%s, delta=%d, new_quantity=%d",
             book_id, delta, new_qty);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"msg\":\"Quantity updated\",\"new_quantity\":%d}",
             new_qty);
    return send_response(connection, MHD_HTTP_OK, resp);
}

/* ==================== 主路由分发 ==================== */
static enum MHD_Result answer_to_connection(void *cls,
                                            struct MHD_Connection *connection,
                                            const char *url,
                                            const char *method,
                                            const char *version,
                                            const char *upload_data,
                                            size_t *upload_data_size,
                                            void **con_cls) {
    (void)cls; (void)version;

    if (strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *r = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(r, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(r, "Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
        MHD_add_response_header(r, "Access-Control-Allow-Headers", "Content-Type");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, r);
        MHD_destroy_response(r);
        return ret;
    }

    if ((strcmp(method, "POST") == 0 || strcmp(method, "DELETE") == 0) && *con_cls == NULL) {
        ReqBuf *rb = malloc(sizeof(ReqBuf));
        if (!rb) return MHD_NO;
        rb->len = 0;
        rb->buf[0] = '\0';
        *con_cls = rb;
        return MHD_YES;
    }

    if ((strcmp(method, "POST") == 0 || strcmp(method, "DELETE") == 0) && *upload_data_size > 0) {
        ReqBuf *rb = (ReqBuf*)(*con_cls);
        size_t space = sizeof(rb->buf) - rb->len - 1;
        if (space > 0) {
            size_t take = *upload_data_size < space ? *upload_data_size : space;
            memcpy(rb->buf + rb->len, upload_data, take);
            rb->len += take;
            rb->buf[rb->len] = '\0';
        }
        *upload_data_size = 0;
        return MHD_YES;
    }

    char *body = NULL;
    if (*con_cls) body = ((ReqBuf*)(*con_cls))->buf;

    enum MHD_Result ret = MHD_NO;

    if (strcmp(method, "GET") == 0) {
        if (strcmp(url, "/") == 0) {
            ret = send_response(connection, MHD_HTTP_OK, "{\"status\":\"running\"}");
        } else if (strcmp(url, "/readers") == 0) {
            ret = handle_get_readers(connection);
        } else if (strcmp(url, "/login") == 0) {
            ret = handle_login(connection, url);
        } else if (strcmp(url, "/books") == 0) {
            ret = handle_get_books(connection);
        } else if (strncmp(url, "/myrecords", 10) == 0) {
            ret = handle_myrecords(connection, url);
        } else if (strcmp(url, "/overdue") == 0) {
            ret = handle_overdue(connection);
        } else {
            ret = send_response(connection, MHD_HTTP_NOT_FOUND,
                                "{\"error\":\"Not Found\"}");
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(url, "/register") == 0) {
            ret = handle_register(connection, body);
        } else if (strcmp(url, "/add-book") == 0) {
            ret = handle_add_book(connection, body);
        } else if (strcmp(url, "/borrow") == 0) {
            ret = handle_borrow(connection, body);
        } else if (strcmp(url, "/update-quantity") == 0) {
            ret = handle_update_quantity(connection, body);
        } else {
            ret = send_response(connection, MHD_HTTP_NOT_FOUND,
                                "{\"error\":\"Not Found\"}");
        }
    } else if (strcmp(method, "DELETE") == 0) {
        if (strcmp(url, "/remove-book") == 0) {
            ret = handle_remove_book(connection, body);
        } else {
            ret = send_response(connection, MHD_HTTP_NOT_FOUND,
                                "{\"error\":\"Not Found\"}");
        }
    } else {
        ret = send_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                            "{\"error\":\"Method not allowed\"}");
    }

    if (*con_cls) {
        free(*con_cls);
        *con_cls = NULL;
    }
    return ret;
}

/* ==================== 主函数 ==================== */
int main(void) {
    load_data();
    init_sample_data();

    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        PORT,
        NULL, NULL,
        &answer_to_connection,
        NULL,
        MHD_OPTION_END);

    if (!daemon) {
        LOG_ERROR("无法启动服务器，端口 %d 可能被占用", PORT);
        return 1;
    }

    LOG_INFO("✅ 服务器启动成功，监听端口 %d", PORT);
    LOG_INFO("访问地址: http://127.0.0.1:%d", PORT);
    LOG_INFO("按 Enter 键停止服务...");
    getchar();

    MHD_stop_daemon(daemon);
    LOG_INFO("服务器已停止");
    return 0;
}