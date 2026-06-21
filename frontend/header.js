// 全局导航栏渲染
function renderHeader() {
    const header = document.getElementById('app-header');
    if (!header) return;

    const readerId = localStorage.getItem('reader_id') || null;
    let userName = '游客';
    if (readerId) {
        userName = localStorage.getItem('reader_name') || readerId;
    }

    const navHtml = `
        <div class="navbar">
            <div class="nav-left">
                <a href="books.html" class="brand">📚 图书馆系统</a>
                <a href="books.html" class="nav-link">图书浏览</a>
                <a href="myrecords.html" class="nav-link">我的借阅</a>
                <a href="admin.html" class="nav-link">管理员</a>
            </div>
            <div class="nav-right">
                ${readerId ? `
                    <span class="user-info">
                        <span class="avatar">👤</span>
                        <span class="username">${userName}</span>
                        <button class="logout-btn" onclick="doLogout()">登出</button>
                    </span>
                ` : `
                    <a href="index.html" class="login-link">登录</a>
                    <a href="register.html" class="register-link">注册</a>
                `}
            </div>
        </div>
    `;
    header.innerHTML = navHtml;
}

// 登出函数
function doLogout() {
    localStorage.removeItem('reader_id');
    localStorage.removeItem('reader_name');
    window.location.reload();
}

// 页面加载时自动渲染
document.addEventListener('DOMContentLoaded', function() {
    renderHeader();
});