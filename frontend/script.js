const API = 'http://localhost:8888';

// 模拟并发借书
async function simulateConcurrentBorrow() {
    const resultDiv = document.getElementById('concurrent-result');
    resultDiv.textContent = '正在测试，请稍候...';
    try {
        const res = await fetch(`${API}/simulate-borrow`, {
            method: 'POST',
            mode: 'cors'
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json();
        resultDiv.innerHTML = `
            测试完成！<br>
            成功借阅：<strong>${data.success_count}</strong> 次<br>
            发生冲突：<strong>${data.fail_count}</strong> 次
        `;
    } catch (err) {
        resultDiv.innerHTML = `❌ 请求失败：${err.message}<br>请确保后端服务器已启动。`;
    }
}

// 以下为占位函数，可自行扩展后端接口后完善
function addReader() {
    alert('读者注册功能待实现（需后端配合）');
}

function addBook() {
    alert('图书录入功能待实现（需后端配合）');
}

function deleteBook() {
    alert('图书撤销功能待实现（需后端配合）');
}