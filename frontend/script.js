const API = 'http://localhost:8888';

function getLoggedReader() {
    return localStorage.getItem('reader_id') || null;
}

function setLoggedReader(id) {
    localStorage.setItem('reader_id', id);
}

function logout() {
    localStorage.removeItem('reader_id');
    localStorage.removeItem('reader_name');
    window.location.href = 'index.html';
}

async function apiFetch(endpoint, options = {}) {
    const url = API + endpoint;
    console.log('[Frontend] apiFetch called with endpoint:', endpoint);
    console.log('[Frontend] Full URL:', url);
    const defaultHeaders = { 'Content-Type': 'application/json' };
    const config = {
        mode: 'cors',
        headers: { ...defaultHeaders, ...(options.headers || {}) },
        ...options
    };
    if (options.body && typeof options.body === 'object') {
        config.body = JSON.stringify(options.body);
    }
    console.log('[API Request]', url, config);
    try {
        const response = await fetch(url, config);
        const text = await response.text();
        console.log('[API Response]', text);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${text}`);
        }
        // 尝试解析 JSON，若失败则抛出带有原始文本的错误
        try {
            return JSON.parse(text);
        } catch (jsonErr) {
            console.error('[JSON Parse Error]', jsonErr);
            throw new Error(`Invalid JSON response: ${text}`);
        }
    } catch (err) {
        console.error('[API Error]', err);
        throw err;
    }
}

// ----- 封装 API 方法 -----
async function getReaders() {
    return await apiFetch('/readers');
}

async function registerReader(id, name, gender) {
    return await apiFetch('/register', {
        method: 'POST',
        body: { id, name, gender }
    });
}

async function getBooks() {
    return await apiFetch('/books');
}

async function apiBorrowBook(reader_id, book_id, days = 30) {
    console.log('apiBorrowBook called with:', { reader_id, book_id, days });
    if (!reader_id || !book_id) {
        throw new Error('reader_id or book_id is empty');
    }
    return await apiFetch('/borrow', {
        method: 'POST',
        body: { reader_id, book_id, days }
    });
}

async function getMyRecords(reader_id) {
    console.log('[Frontend] getMyRecords called with reader_id:', reader_id);
    return await apiFetch(`/myrecords?reader_id=${reader_id}`);
}

async function getOverdueRecords() {
    return await apiFetch('/overdue');
}

async function addBook(book) {
    return await apiFetch('/add-book', {
        method: 'POST',
        body: book
    });
}

async function removeBook(book_id) {
    return await apiFetch('/remove-book', {
        method: 'DELETE',
        body: { id: book_id }
    });
}

// ----- 新增：调整馆藏数量（已修复 delta 为字符串） -----
async function updateBookQuantity(bookId, delta) {
    if (!bookId || delta === undefined) {
        throw new Error('Missing bookId or delta');
    }
    // 将 delta 转为字符串，以符合后端 json_get_string 解析
    return await apiFetch('/update-quantity', {
        method: 'POST',
        body: { book_id: bookId, delta: String(delta) }
    });
}