async function adminLogin() {
  const email = document.getElementById("adminEmail").value.trim();
  const password = document.getElementById("adminPassword").value;
  const r = await api("/admin/login", "POST", { email, password });
  if (!r.ok) {
    setStatus(r.message || "Admin login failed");
    return;
  }
  localStorage.setItem("adminLoggedIn", "1");
  window.location.href = "admin-dashboard.html";
}
