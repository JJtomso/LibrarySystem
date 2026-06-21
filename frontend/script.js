const API = 'http://localhost:8888';

// ---------- 登录状态管理 ----------
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

// ---------- 公共工具 ----------
function requireLogin() {
    const id = getLoggedReader();
    if (!id) {
        alert('请先登录');
        window.location.href = 'index.html';
        return null;
    }
    return id;
}

// ---------- API 封装 ----------
async function apiFetch(endpoint, options = {}) {
    const url = API + endpoint;
    console.log('[API]', url, options);

    const headers = { 'Content-Type': 'application/json' };
    if (options.headers) Object.assign(headers, options.headers);

    const config = {
        mode: 'cors',
        headers,
        ...options
    };

    if (options.body && typeof options.body === 'object') {
        config.body = JSON.stringify(options.body);
    }

    try {
        const response = await fetch(url, config);
        const text = await response.text();
        console.log('[API Response]', text);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${text}`);
        }
        return JSON.parse(text);
    } catch (err) {
        console.error('[API Error]', err);
        throw err;
    }
}

// ---------- API 方法 ----------
async function loginCheck(readerId) {
    return await apiFetch(`/login?reader_id=${readerId}`);
}

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
    return await apiFetch('/borrow', {
        method: 'POST',
        body: { reader_id, book_id, days }
    });
}

async function getMyRecords(reader_id) {
    return await apiFetch(`/myrecords?reader_id=${reader_id}`);
}

async function getOverdueRecords() {
    return await apiFetch('/overdue');
}

async function apiAddBook(book) {
    return await apiFetch('/add-book', {
        method: 'POST',
        body: book
    });
}

async function apiRemoveBook(book_id) {
    return await apiFetch('/remove-book', {
        method: 'DELETE',
        body: { id: book_id }
    });
}

async function apiUpdateQuantity(bookId, delta) {
    return await apiFetch('/update-quantity', {
        method: 'POST',
        body: { book_id: bookId, delta: String(delta) }
    });
}