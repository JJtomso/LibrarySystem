function renderHeader() {
    const header = document.getElementById('app-header');
    if (!header) return;

    const username = getUsername();
    const role = getRole();
    let userName = '游客';
    if (username) {
        userName = getUserName();
    }

    // 根据角色生成导航链接
    let navLinks = '';
    if (role === 'student') {
        navLinks = `
            <a href="books.html" class="nav-link">图书浏览</a>
            <a href="myrecords.html" class="nav-link">我的借阅</a>
        `;
    } else if (role === 'admin') {
        navLinks = `
            <a href="books.html" class="nav-link">图书浏览</a>
            <a href="admin.html" class="nav-link">管理员</a>
        `;
    } else {
        // 未登录
        navLinks = `
            <a href="books.html" class="nav-link">图书浏览</a>
        `;
    }

    const navHtml = `
        <div class="navbar">
            <div class="nav-left">
                <a href="books.html" class="brand">📚 图书馆系统</a>
                ${navLinks}
            </div>
            <div class="nav-right">
                ${username ? `
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

function doLogout() {
    localStorage.removeItem('username');
    localStorage.removeItem('role');
    localStorage.removeItem('name');
    localStorage.removeItem('reader_id');
    window.location.reload();
}

document.addEventListener('DOMContentLoaded', renderHeader);