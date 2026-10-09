"use strict";
// All optional interactions enhance native navigation and forms; no external dependencies.
const filter = document.querySelector("[data-table-filter]");
if (filter) {
  const rows = [...filter.closest("section").querySelectorAll("tbody tr")];
  const counter = document.querySelector("[data-filter-count]");
  filter.addEventListener("input", () => {
    const needle = filter.value.trim().toLocaleLowerCase();
    let visible = 0;
    rows.forEach(row => {
      row.hidden = !row.textContent.toLocaleLowerCase().includes(needle);
      if (!row.hidden) visible += 1;
    });
    counter.textContent = `显示 ${visible} / ${rows.length} 个页面`;
  });
}
// Immediate local feedback. Clipboard errors remain visible and allow manual selection.
document.querySelectorAll("pre:not([aria-live])").forEach(pre => {
  const wrapper = document.createElement("div");
  wrapper.className = "code-wrap";
  pre.before(wrapper);
  wrapper.append(pre);
  const button = document.createElement("button");
  button.type = "button";
  button.className = "copy-button";
  button.textContent = "复制";
  button.setAttribute("aria-label", "复制这段命令或文本");
  const feedback = document.createElement("span");
  feedback.className = "copy-feedback";
  feedback.setAttribute("role", "status");
  wrapper.append(button, feedback);
  button.addEventListener("click", async () => {
    feedback.textContent = "正在复制…";
    try {
      await navigator.clipboard.writeText(pre.textContent);
      feedback.textContent = "已复制";
    } catch (_) {
      const range = document.createRange();
      range.selectNodeContents(pre);
      const selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      feedback.textContent = "文本已选中，请手动复制";
    }
  });
});
const statusButton = document.querySelector("[data-check-status]");
if (statusButton) {
  let count = 0;
  let controller;
  statusButton.addEventListener("click", async () => {
    if (controller) controller.abort();
    const current = new AbortController();
    controller = current;
    const result = document.querySelector("[data-status-result]");
    result.textContent = "正在检查 /api/status…";
    const started = performance.now();
    document.querySelector("[data-status-code]").textContent = "—";
    document.querySelector("[data-status-latency]").textContent = "—";
    try {
      const response = await fetch("/api/status", {cache:"no-store", signal:current.signal});
      const data = await response.json();
      if (controller !== current) return;
      document.querySelector("[data-status-code]").textContent = String(response.status);
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      document.querySelector("[data-status-latency]").textContent = `${Math.round(performance.now()-started)} ms`;
      document.querySelector("[data-status-count]").textContent = String(++count);
      result.textContent = JSON.stringify(data, null, 2);
    } catch (error) {
      if (controller !== current || error.name === "AbortError") return;
      result.textContent = `检查失败：${error.message}。请确认服务已启动。`;
    }
  });
}
const playground = document.querySelector("[data-playground]");
if (playground) {
  let controller;
  const output = document.querySelector("[data-playground-output]");
  playground.addEventListener("submit", async event => {
    event.preventDefault();
    if (controller) controller.abort();
    const current = new AbortController();
    controller = current;
    const method = playground.elements.method.value;
    const endpoint = playground.elements.endpoint.value;
    output.textContent = `${method} ${endpoint}\n正在发送…`;
    const options = {method, signal:current.signal, cache:"no-store"};
    if (method === "POST" || method === "PUT") {
      options.body = playground.elements.payload.value;
      options.headers = {"Content-Type":"text/plain; charset=utf-8"};
    }
    try {
      const response = await fetch(endpoint, options);
      const text = await response.text();
      if (controller !== current) return;
      output.textContent = `${method} ${endpoint}\nHTTP ${response.status} ${response.statusText}\nContent-Type: ${response.headers.get("Content-Type") || "未提供"}\nContent-Length: ${response.headers.get("Content-Length") || "未提供"}\n\n${text}`;
    } catch (error) {
      if (controller !== current || error.name === "AbortError") return;
      output.textContent = `请求失败：${error.message}`;
    }
  });
  document.querySelector("[data-cancel-request]").addEventListener("click", () => {
    if (controller) controller.abort();
    controller = undefined;
    output.textContent = "当前请求已取消。你可以修改内容后重新发送。";
  });
}
