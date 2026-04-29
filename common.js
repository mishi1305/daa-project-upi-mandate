const API = "http://localhost:8080/api";

async function api(path, method = "GET", data = null) {
  const options = { method, headers: { "Content-Type": "application/json" } };
  if (data) options.body = JSON.stringify(data);
  const res = await fetch(`${API}${path}`, options);
  return res.json();
}

function money(x) {
  return Number(x).toFixed(2);
}

function setStatus(message) {
  const el = document.getElementById("status");
  if (el) el.textContent = message;
}

function saveUserSession(userId, name) {
  localStorage.setItem("userId", String(userId));
  localStorage.setItem("userName", name || "");
}

function clearUserSession() {
  localStorage.removeItem("userId");
  localStorage.removeItem("userName");
}

function getUserId() {
  return Number(localStorage.getItem("userId") || 0);
}
