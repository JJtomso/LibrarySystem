/**
 * 图书馆管理系统 - 后端服务
 * 基于 libmicrohttpd，提供 RESTful API
 * 数据持久化至二进制文件 library.dat
 */

#define _XOPEN_SOURCE 700  // 启用 strptime 等 POSIX 函数

#include <microhttpd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

/* ==================== 常量 ==================== */
#define PORT                8888
#define MAX_READERS         1000
#define MAX_BOOKS           1000
#define MAX_RECORDS         5000
#define DATA_FILE           "library.dat"
#define BUFFER_SIZE         32768
#define DATE_STR_LEN        20

/* ==================== 数据结构 ==================== */
typedef struct {
    char id[10];
    char name[50];
    char gender[4];
} Reader;

typedef struct {
    char book_id[20];
    char name[100];
    char author[50];
    char publisher[50];
    double price;
    char location[50];
    int quantity;
    int borrowed;
} Book;

typedef struct {
    char reader_id[10];
    char book_id[20];
    char borrow_date[DATE_STR_LEN];
    char due_date[DATE_STR_LEN];
    char return_date[DATE_STR_LEN];
} BorrowRecord;

/* ==================== 请求缓冲区 ==================== */
typedef struct {
    char buf[BUFFER_SIZE];
    size_t len;
} ReqBuf;

/* 参数解析上下文 */
typedef struct {
    char id[10];
    int found;
} ParamContext;

/* ==================== 全局数据 ==================== */
static Reader readers[MAX_READERS];
static int reader_count = 0;
static Book books[MAX_BOOKS];
static int book_count = 0;
static BorrowRecord records[MAX_RECORDS];
static int record_count = 0;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ==================== 日志 ==================== */
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
    if (!fp) { LOG_ERROR("无法打开数据文件写入"); return; }
    fwrite(&reader_count, sizeof(int), 1, fp);
    fwrite(&book_count, sizeof(int), 1, fp);
    fwrite(&record_count, sizeof(int), 1, fp);
    if (reader_count > 0) fwrite(readers, sizeof(Reader), reader_count, fp);
    if (book_count > 0) fwrite(books, sizeof(Book), book_count, fp);
    if (record_count > 0) fwrite(records, sizeof(BorrowRecord), record_count, fp);
    fflush(fp); fsync(fileno(fp)); fclose(fp);
    LOG_INFO("数据已保存 (读者:%d, 图书:%d, 记录:%d)", reader_count, book_count, record_count);
}

void load_data(void) {
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) { 
        reader_count = 0;
        book_count = 0;
        record_count = 0;
        LOG_INFO("数据文件不存在，初始化空数据");
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
    LOG_INFO("加载数据成功 (读者:%d, 图书:%d, 记录:%d)", reader_count, book_count, record_count);
}

/* ==================== 初始化样例数据 ==================== */
void init_sample_data(void) {
    if (reader_count > 0 || book_count > 0) return;
    
    strcpy(readers[0].id, "202100101");
    strcpy(readers[0].name, "张三");
    strcpy(readers[0].gender, "男");
    reader_count = 1;

    strcpy(books[0].book_id, "TP312C");
    strcpy(books[0].name, "C程序设计");
    strcpy(books[0].author, "谭浩强");
    strcpy(books[0].publisher, "清华大学出版社");
    books[0].price = 39.00;
    strcpy(books[0].location, "A区3排");
    books[0].quantity = 5;
    books[0].borrowed = 1;

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

static int days_diff(const char *date1, const char *date2) {
    struct tm tm1 = {0}, tm2 = {0};
    strptime(date1, "%Y-%m-%d", &tm1);
    strptime(date2, "%Y-%m-%d", &tm2);
    time_t t1 = mktime(&tm1);
    time_t t2 = mktime(&tm2);
    return (int)((t2 - t1) / 86400);
}

/* JSON 解析函数 - 直接从 JSON 字符串提取值 */
static char* json_extract_string(const char *json, const char *key, char *out, size_t out_size) {
    out[0] = '\0';
    if (!json || !key) return out;
    
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return out;
    
    p = strchr(p, ':');
    if (!p) return out;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        if (!end) return out;
        size_t len = end - p;
        if (len >= out_size) len = out_size - 1;
        memcpy(out, p, len);
        out[len] = '\0';
    } else {
        // 数字或布尔值
        const char *end = p;
        while (*end && *end != ',' && *end != '}' && *end != ' ' && *end != '\t' && *end != '\n') end++;
        size_t len = end - p;
        if (len >= out_size) len = out_size - 1;
        memcpy(out, p, len);
        out[len] = '\0';
    }
    return out;
}

static double json_extract_double(const char *json, const char *key) {
    char buf[64];
    json_extract_string(json, key, buf, sizeof(buf));
    return atof(buf);
}

/* ==================== HTTP 响应辅助 ==================== */
static enum MHD_Result send_json(struct MHD_Connection *conn, int status, const char *json) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(
        strlen(json), (void*)json, MHD_RESPMEM_MUST_COPY);
    if (!resp) return MHD_NO;
    MHD_add_response_header(resp, "Content-Type", "application/json");
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
    enum MHD_Result ret = MHD_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

/* 参数解析回调函数 */
static enum MHD_Result param_callback(void *cls, enum MHD_ValueKind kind,
                                      const char *key, const char *value) {
    ParamContext *ctx = (ParamContext*)cls;
    if (strcmp(key, "reader_id") == 0) {
        strncpy(ctx->id, value, sizeof(ctx->id) - 1);
        ctx->id[sizeof(ctx->id) - 1] = '\0';
        ctx->found = 1;
        return MHD_NO;
    }
    return MHD_YES;
}

/* ==================== 业务逻辑 ==================== */
static int register_reader_internal(const char *id, const char *name, const char *gender) {
    for (int i = 0; i < reader_count; i++) {
        if (strcmp(readers[i].id, id) == 0) return -1;
    }
    if (reader_count >= MAX_READERS) return -2;
    
    strncpy(readers[reader_count].id, id, sizeof(readers[0].id) - 1);
    strncpy(readers[reader_count].name, name, sizeof(readers[0].name) - 1);
    strncpy(readers[reader_count].gender, gender, sizeof(readers[0].gender) - 1);
    reader_count++;
    return 0;
}

static int borrow_book_internal(const char *reader_id, const char *book_id, 
                                int days, char *out_due_date) {
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

    // 检查是否已借未还
    for (int i = 0; i < record_count; i++) {
        if (strcmp(records[i].reader_id, reader_id) == 0 &&
            strcmp(records[i].book_id, book_id) == 0 &&
            records[i].return_date[0] == '\0') {
            return -5;
        }
    }

    BorrowRecord *rec = &records[record_count];
    strncpy(rec->reader_id, reader_id, sizeof(rec->reader_id) - 1);
    strncpy(rec->book_id, book_id, sizeof(rec->book_id) - 1);
    today_str(rec->borrow_date, sizeof(rec->borrow_date));
    future_date(days, rec->due_date, sizeof(rec->due_date));
    rec->return_date[0] = '\0';
    record_count++;
    books[book_idx].borrowed++;
    if (out_due_date) strcpy(out_due_date, rec->due_date);
    return 0;
}

/* ==================== 请求处理函数 ==================== */

static enum MHD_Result handle_options(struct MHD_Connection *conn) {
    struct MHD_Response *r = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(r, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(r, "Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
    MHD_add_response_header(r, "Access-Control-Allow-Headers", "Content-Type");
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, r);
    MHD_destroy_response(r);
    return ret;
}

/* GET /readers */
static enum MHD_Result handle_get_readers(struct MHD_Connection *conn) {
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
    return send_json(conn, MHD_HTTP_OK, tmp);
}

/* POST /register */
static enum MHD_Result handle_register(struct MHD_Connection *conn, const char *body) {
    if (!body || !*body) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Empty body\"}");
    }
    
    char id[10] = {0}, name[50] = {0}, gender[4] = {0};
    json_extract_string(body, "id", id, sizeof(id));
    json_extract_string(body, "name", name, sizeof(name));
    json_extract_string(body, "gender", gender, sizeof(gender));
    
    if (!id[0] || !name[0] || !gender[0]) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Missing fields\"}");
    }
    
    pthread_mutex_lock(&data_mutex);
    int ret = register_reader_internal(id, name, gender);
    if (ret == 0) {
        save_data();
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("新读者注册: %s %s", id, name);
        return send_json(conn, MHD_HTTP_OK, "{\"status\":\"ok\"}");
    } else {
        pthread_mutex_unlock(&data_mutex);
        if (ret == -1) {
            return send_json(conn, MHD_HTTP_BAD_REQUEST, 
                "{\"error\":\"Already exists\",\"code\":\"ALREADY_EXISTS\"}");
        } else {
            return send_json(conn, MHD_HTTP_BAD_REQUEST, 
                "{\"error\":\"Reader list full\"}");
        }
    }
}

/* GET /books */
static enum MHD_Result handle_get_books(struct MHD_Connection *conn) {
    pthread_mutex_lock(&data_mutex);
    char tmp[BUFFER_SIZE];
    int pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "[");
    for (int i = 0; i < book_count; i++) {
        if (i > 0) pos += snprintf(tmp + pos, sizeof(tmp) - pos, ",");
        int available = books[i].quantity - books[i].borrowed;
        char status[32];
        snprintf(status, sizeof(status), available > 0 ? "馆藏(余%d)" : "已借出", available);
        pos += snprintf(tmp + pos, sizeof(tmp) - pos,
            "{\"id\":\"%s\",\"name\":\"%s\",\"author\":\"%s\",\"publisher\":\"%s\","
            "\"price\":%.2f,\"location\":\"%s\",\"quantity\":%d,\"borrowed\":%d,\"status\":\"%s\"}",
            books[i].book_id, books[i].name, books[i].author, books[i].publisher,
            books[i].price, books[i].location, books[i].quantity, books[i].borrowed, status);
    }
    snprintf(tmp + pos, sizeof(tmp) - pos, "]");
    pthread_mutex_unlock(&data_mutex);
    return send_json(conn, MHD_HTTP_OK, tmp);
}

/* POST /add-book */
static enum MHD_Result handle_add_book(struct MHD_Connection *conn, const char *body) {
    if (!body || !*body) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Empty\"}");
    }
    
    char id[20] = {0}, name[100] = {0}, author[50] = {0};
    char publisher[50] = {0}, location[50] = {0};
    json_extract_string(body, "id", id, sizeof(id));
    json_extract_string(body, "name", name, sizeof(name));
    json_extract_string(body, "author", author, sizeof(author));
    json_extract_string(body, "publisher", publisher, sizeof(publisher));
    json_extract_string(body, "location", location, sizeof(location));
    double price = json_extract_double(body, "price");
    
    if (!id[0] || !name[0]) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Missing id/name\"}");
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
        LOG_INFO("图书数量增加: %s", id);
        return send_json(conn, MHD_HTTP_OK, "{\"status\":\"ok\",\"msg\":\"quantity increased\"}");
    } else {
        if (book_count >= MAX_BOOKS) {
            pthread_mutex_unlock(&data_mutex);
            return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Book list full\"}");
        }
        strncpy(books[book_count].book_id, id, sizeof(books[0].book_id) - 1);
        strncpy(books[book_count].name, name, sizeof(books[0].name) - 1);
        strncpy(books[book_count].author, author, sizeof(books[0].author) - 1);
        strncpy(books[book_count].publisher, publisher, sizeof(books[0].publisher) - 1);
        books[book_count].price = price;
        strncpy(books[book_count].location, location, sizeof(books[0].location) - 1);
        books[book_count].quantity = 1;
        books[book_count].borrowed = 0;
        book_count++;
        save_data();
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("新增图书: %s %s", id, name);
        return send_json(conn, MHD_HTTP_OK, "{\"status\":\"ok\",\"msg\":\"new book added\"}");
    }
}

/* DELETE /remove-book - 彻底删除整本图书 */
static enum MHD_Result handle_remove_book(struct MHD_Connection *conn, const char *body) {
    if (!body || !*body) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Empty body\"}");
    }
    
    char id[20] = {0};
    json_extract_string(body, "id", id, sizeof(id));
    if (!id[0]) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Missing book id\"}");
    }
    
    pthread_mutex_lock(&data_mutex);
    
    int idx = -1;
    for (int i = 0; i < book_count; i++) {
        if (strcmp(books[i].book_id, id) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        pthread_mutex_unlock(&data_mutex);
        return send_json(conn, MHD_HTTP_NOT_FOUND, 
            "{\"error\":\"Book not found\"}");
    }
    
    // 检查是否有未归还的借阅
    if (books[idx].borrowed > 0) {
        pthread_mutex_unlock(&data_mutex);
        char resp[256];
        snprintf(resp, sizeof(resp),
            "{\"error\":\"Cannot delete book with borrowed copies\","
            "\"borrowed\":%d,\"quantity\":%d}",
            books[idx].borrowed, books[idx].quantity);
        return send_json(conn, MHD_HTTP_BAD_REQUEST, resp);
    }
    
    // 完全删除该图书记录
    int deleted_qty = books[idx].quantity;
    for (int i = idx; i < book_count - 1; i++) {
        books[i] = books[i + 1];
    }
    book_count--;
    save_data();
    pthread_mutex_unlock(&data_mutex);
    
    LOG_INFO("图书已彻底删除: %s (共 %d 册)", id, deleted_qty);
    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"msg\":\"Book removed completely\","
        "\"removed_quantity\":%d}", deleted_qty);
    return send_json(conn, MHD_HTTP_OK, resp);
}

/* POST /borrow */
static enum MHD_Result handle_borrow(struct MHD_Connection *conn, const char *body) {
    if (!body || !*body) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Empty\"}");
    }
    
    char reader_id[10] = {0}, book_id[20] = {0}, days_str[10] = "30";
    json_extract_string(body, "reader_id", reader_id, sizeof(reader_id));
    json_extract_string(body, "book_id", book_id, sizeof(book_id));
    json_extract_string(body, "days", days_str, sizeof(days_str));
    
    if (!reader_id[0] || !book_id[0]) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, 
            "{\"error\":\"Missing reader_id or book_id\"}");
    }
    
    int days = atoi(days_str);
    if (days <= 0) days = 30;
    
    pthread_mutex_lock(&data_mutex);
    char due_date[DATE_STR_LEN];
    int ret = borrow_book_internal(reader_id, book_id, days, due_date);
    
    if (ret == 0) {
        save_data();
        pthread_mutex_unlock(&data_mutex);
        LOG_INFO("借书成功: %s -> %s, 应还 %s", reader_id, book_id, due_date);
        char resp[256];
        snprintf(resp, sizeof(resp), 
            "{\"status\":\"ok\",\"msg\":\"borrowed\",\"due_date\":\"%s\"}", due_date);
        return send_json(conn, MHD_HTTP_OK, resp);
    } else {
        pthread_mutex_unlock(&data_mutex);
        const char *err;
        switch(ret) {
            case -1: err = "reader not found"; break;
            case -2: err = "book not found"; break;
            case -3: err = "no available copy"; break;
            case -4: err = "record list full"; break;
            case -5: err = "already borrowed and not returned"; break;
            default: err = "unknown error";
        }
        LOG_INFO("借书失败: %s", err);
        char resp[256];
        snprintf(resp, sizeof(resp), "{\"error\":\"%s\"}", err);
        int code = (ret == -1) ? MHD_HTTP_FORBIDDEN : 
                   (ret == -2) ? MHD_HTTP_NOT_FOUND : MHD_HTTP_BAD_REQUEST;
        return send_json(conn, code, resp);
    }
}

/* GET /myrecords */
static enum MHD_Result handle_myrecords(struct MHD_Connection *conn) {
    ParamContext ctx = { .id = {0}, .found = 0 };
    MHD_get_connection_values(conn, MHD_GET_ARGUMENT_KIND, param_callback, &ctx);
    
    if (!ctx.found || !ctx.id[0]) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"missing reader_id\"}");
    }
    
    pthread_mutex_lock(&data_mutex);
    char tmp[BUFFER_SIZE];
    int pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "[");
    char today[DATE_STR_LEN];
    today_str(today, sizeof(today));
    int first = 1;
    
    for (int i = 0; i < record_count; i++) {
        if (strcmp(records[i].reader_id, ctx.id) != 0) continue;
        if (!first) pos += snprintf(tmp + pos, sizeof(tmp) - pos, ",");
        first = 0;
        
        int overdue = 0;
        if (records[i].return_date[0] == '\0') {
            int diff = days_diff(records[i].due_date, today);
            if (diff > 0) overdue = diff;
        }
        
        pos += snprintf(tmp + pos, sizeof(tmp) - pos,
            "{\"book_id\":\"%s\",\"borrow_date\":\"%s\",\"due_date\":\"%s\","
            "\"return_date\":\"%s\",\"overdue_days\":%d}",
            records[i].book_id, records[i].borrow_date, records[i].due_date,
            records[i].return_date[0] ? records[i].return_date : "未还", overdue);
    }
    snprintf(tmp + pos, sizeof(tmp) - pos, "]");
    pthread_mutex_unlock(&data_mutex);
    return send_json(conn, MHD_HTTP_OK, tmp);
}

/* GET /overdue */
static enum MHD_Result handle_overdue(struct MHD_Connection *conn) {
    pthread_mutex_lock(&data_mutex);
    char tmp[BUFFER_SIZE];
    int pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "[");
    char today[DATE_STR_LEN];
    today_str(today, sizeof(today));
    int first = 1;
    
    for (int i = 0; i < record_count; i++) {
        if (records[i].return_date[0] != '\0') continue;
        int diff = days_diff(records[i].due_date, today);
        if (diff <= 0) continue;
        if (!first) pos += snprintf(tmp + pos, sizeof(tmp) - pos, ",");
        first = 0;
        
        char name[50] = "未知";
        for (int j = 0; j < reader_count; j++) {
            if (strcmp(readers[j].id, records[i].reader_id) == 0) {
                strncpy(name, readers[j].name, sizeof(name) - 1);
                break;
            }
        }
        
        pos += snprintf(tmp + pos, sizeof(tmp) - pos,
            "{\"reader_id\":\"%s\",\"reader_name\":\"%s\",\"book_id\":\"%s\","
            "\"borrow_date\":\"%s\",\"due_date\":\"%s\",\"overdue_days\":%d}",
            records[i].reader_id, name, records[i].book_id,
            records[i].borrow_date, records[i].due_date, diff);
    }
    snprintf(tmp + pos, sizeof(tmp) - pos, "]");
    pthread_mutex_unlock(&data_mutex);
    return send_json(conn, MHD_HTTP_OK, tmp);
}

/* GET /login */
static enum MHD_Result handle_login(struct MHD_Connection *conn) {
    ParamContext ctx = { .id = {0}, .found = 0 };
    MHD_get_connection_values(conn, MHD_GET_ARGUMENT_KIND, param_callback, &ctx);
    
    if (!ctx.found || !ctx.id[0]) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"missing reader_id\"}");
    }
    
    pthread_mutex_lock(&data_mutex);
    int exists = 0;
    for (int i = 0; i < reader_count; i++) {
        if (strcmp(readers[i].id, ctx.id) == 0) { exists = 1; break; }
    }
    pthread_mutex_unlock(&data_mutex);
    
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"exists\":%s}", exists ? "true" : "false");
    return send_json(conn, MHD_HTTP_OK, resp);
}

/* POST /update-quantity */
static enum MHD_Result handle_update_quantity(struct MHD_Connection *conn, const char *body) {
    if (!body || !*body) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Empty\"}");
    }
    
    char book_id[20] = {0}, delta_str[12] = {0};
    json_extract_string(body, "book_id", book_id, sizeof(book_id));
    json_extract_string(body, "delta", delta_str, sizeof(delta_str));
    
    if (!book_id[0] || !delta_str[0]) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, 
            "{\"error\":\"Missing book_id or delta\"}");
    }
    
    int delta = atoi(delta_str);
    if (delta == 0) {
        return send_json(conn, MHD_HTTP_BAD_REQUEST, 
            "{\"error\":\"Delta must be non-zero\"}");
    }
    
    pthread_mutex_lock(&data_mutex);
    int idx = -1;
    for (int i = 0; i < book_count; i++) {
        if (strcmp(books[i].book_id, book_id) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        pthread_mutex_unlock(&data_mutex);
        return send_json(conn, MHD_HTTP_NOT_FOUND, "{\"error\":\"Book not found\"}");
    }
    
    int new_qty = books[idx].quantity + delta;
    if (new_qty < 0 || new_qty < books[idx].borrowed) {
        pthread_mutex_unlock(&data_mutex);
        return send_json(conn, MHD_HTTP_BAD_REQUEST, "{\"error\":\"Invalid quantity\"}");
    }
    
    books[idx].quantity = new_qty;
    save_data();
    pthread_mutex_unlock(&data_mutex);
    
    char resp[128];
    snprintf(resp, sizeof(resp), 
        "{\"status\":\"ok\",\"msg\":\"Updated\",\"new_quantity\":%d}", new_qty);
    return send_json(conn, MHD_HTTP_OK, resp);
}

/* ==================== 路由分发 ==================== */
static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *conn,
                                            const char *url, const char *method,
                                            const char *version, const char *upload_data,
                                            size_t *upload_data_size, void **con_cls) {
    (void)cls; (void)version;
    
    if (strcmp(method, "OPTIONS") == 0) {
        return handle_options(conn);
    }
    
    // 处理 POST/DELETE 的 body
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
    
    const char *body = (*con_cls) ? ((ReqBuf*)(*con_cls))->buf : NULL;
    enum MHD_Result ret = MHD_NO;
    
    if (strcmp(method, "GET") == 0) {
        if (strcmp(url, "/") == 0) {
            ret = send_json(conn, MHD_HTTP_OK, "{\"status\":\"running\"}");
        } else if (strcmp(url, "/readers") == 0) {
            ret = handle_get_readers(conn);
        } else if (strcmp(url, "/login") == 0) {
            ret = handle_login(conn);
        } else if (strcmp(url, "/books") == 0) {
            ret = handle_get_books(conn);
        } else if (strncmp(url, "/myrecords", 10) == 0) {
            ret = handle_myrecords(conn);
        } else if (strcmp(url, "/overdue") == 0) {
            ret = handle_overdue(conn);
        } else {
            ret = send_json(conn, MHD_HTTP_NOT_FOUND, "{\"error\":\"Not Found\"}");
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(url, "/register") == 0) {
            ret = handle_register(conn, body);
        } else if (strcmp(url, "/add-book") == 0) {
            ret = handle_add_book(conn, body);
        } else if (strcmp(url, "/borrow") == 0) {
            ret = handle_borrow(conn, body);
        } else if (strcmp(url, "/update-quantity") == 0) {
            ret = handle_update_quantity(conn, body);
        } else {
            ret = send_json(conn, MHD_HTTP_NOT_FOUND, "{\"error\":\"Not Found\"}");
        }
    } else if (strcmp(method, "DELETE") == 0) {
        if (strcmp(url, "/remove-book") == 0) {
            ret = handle_remove_book(conn, body);
        } else {
            ret = send_json(conn, MHD_HTTP_NOT_FOUND, "{\"error\":\"Not Found\"}");
        }
    } else {
        ret = send_json(conn, MHD_HTTP_METHOD_NOT_ALLOWED, 
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
        PORT, NULL, NULL,
        &answer_to_connection, NULL,
        MHD_OPTION_END);
    
    if (!daemon) {
        LOG_ERROR("启动失败，端口 %d 可能被占用", PORT);
        return 1;
    }
    
    LOG_INFO("✅ 服务器启动成功，监听端口 %d", PORT);
    LOG_INFO("按 Enter 停止...");
    getchar();
    
    MHD_stop_daemon(daemon);
    LOG_INFO("服务器已停止");
    return 0;
}