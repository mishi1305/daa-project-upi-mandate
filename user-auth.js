async function registerUser() {
  const name = document.getElementById("name").value.trim();
  const email = document.getElementById("email").value.trim();
  const password = document.getElementById("password").value;
  const r = await api("/register", "POST", { name, email, password });
  setStatus(r.message || "Register completed");
}

async function loginUser() {
  const email = document.getElementById("email").value.trim();
  const password = document.getElementById("password").value;
  const r = await api("/login", "POST", { email, password });
  if (!r.ok) {
    setStatus(r.message || "Login failed");
    return;
  }
  saveUserSession(r.userId, r.name);
  window.location.href = "user-dashboard.html";
}
