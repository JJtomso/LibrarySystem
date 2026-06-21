const API = 'http://localhost:8888';

// ---------- 用户信息管理（角色、用户名） ----------
function setUserInfo(username, role, name) {
    localStorage.setItem('username', username);
    localStorage.setItem('role', role);
    localStorage.setItem('name', name);
    if (role === 'student') {
        localStorage.setItem('reader_id', username); // 兼容旧逻辑
    } else {
        localStorage.removeItem('reader_id');
    }
}

function getUsername() {
    return localStorage.getItem('username') || null;
}

function getRole() {
    return localStorage.getItem('role') || null;
}

function isLoggedIn() {
    return getUsername() !== null;
}

function getUserName() {
    return localStorage.getItem('name') || getUsername() || '用户';
}

// 兼容旧代码：获取读者学号（仅学生）
function getLoggedReader() {
    if (getRole() === 'student') return getUsername();
    return null;
}

function setLoggedReader(id) {
    // 已被 setUserInfo 替代，保留空函数防错
}

function logout() {
    localStorage.removeItem('username');
    localStorage.removeItem('role');
    localStorage.removeItem('name');
    localStorage.removeItem('reader_id');
    window.location.href = 'index.html';
}

// ---------- 公共工具 ----------
function requireLogin() {
    if (!isLoggedIn()) {
        alert('请先登录');
        window.location.href = 'index.html';
        return null;
    }
    return getUsername();
}

// 限制学生访问
function requireStudent() {
    const username = requireLogin();
    if (!username) return null;
    if (getRole() !== 'student') {
        alert('该功能仅限学生使用');
        window.history.back();
        return null;
    }
    return username;
}

// 限制管理员访问
function requireAdmin() {
    const username = requireLogin();
    if (!username) return null;
    if (getRole() !== 'admin') {
        alert('该功能仅限管理员使用');
        window.history.back();
        return null;
    }
    return username;
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
async function login(username, password) {
    return await apiFetch('/login', {
        method: 'POST',
        body: { username, password }
    });
}

async function changePassword(username, oldPassword, newPassword) {
    return await apiFetch('/change-password', {
        method: 'POST',
        body: { username, old_password: oldPassword, new_password: newPassword }
    });
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