const currentUserId = getUserId();
const userName = localStorage.getItem("userName") || "User";
let mandateCountdownTimer = null;
let bookingCache = [];

function escapeHtml(s) {
  return String(s)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function showPopup(message) {
  const overlay = document.getElementById("popupOverlay");
  const text = document.getElementById("popupMessage");
  if (!overlay || !text) return;
  text.textContent = message || "Something went wrong";
  overlay.style.display = "flex";
}

function closePopup() {
  const overlay = document.getElementById("popupOverlay");
  if (!overlay) return;
  overlay.style.display = "none";
}

window.closePopup = closePopup;

if (!currentUserId) {
  window.location.href = "user-auth.html";
}

document.getElementById("welcomeText").textContent = `Welcome, ${userName}. Manage booking, mandate, and pricing checks.`;

async function refreshFlights() {
  const r = await api("/flights");
  const body = document.getElementById("flightTable");
  body.innerHTML = "";
  if (!r.ok) return setStatus("Could not fetch flights");

  r.flights.forEach(f => {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td>${f.id}</td><td>${f.from} → ${f.to}</td><td>${f.departure}</td><td>${money(f.basePrice)}</td><td>${money(f.currentPrice)}</td><td>${f.seats}</td>`;
    body.appendChild(tr);
  });
  setStatus("Live prices updated");
}

async function createMandate() {
  const payload = {
    userId: currentUserId,
    upiId: document.getElementById("mandateUpiId").value.trim(),
    limitAmount: Number(document.getElementById("mandateLimit").value),
    validityMinutes: Number(document.getElementById("mandateMinutes").value)
  };
  const r = await api("/mandate", "POST", payload);
  setStatus(r.message || "Mandate action completed");
  if (r.ok) {
    loadMandateStatus();
  }
}

async function manualBook() {
  const payload = { userId: currentUserId, flightId: document.getElementById("manualFlightId").value.trim() };
  const r = await api("/book", "POST", payload);
  if (!r.ok) {
    showPopup(r.message || "Booking failed. Could not proceed");
    setStatus(r.message || "Booking failed");
    return;
  }
  setStatus(r.message || "Booking action completed");
  viewBookings();
  refreshFlights();
  loadMandateStatus();
}

async function autoBook() {
  const payload = {
    userId: currentUserId,
    from: document.getElementById("autoFrom").value.trim(),
    to: document.getElementById("autoTo").value.trim(),
    budget: Number(document.getElementById("budget").value),
    latestTime: document.getElementById("latestTime").value.trim()
  };
  const r = await api("/autobook", "POST", payload);
  if (!r.ok) {
    showPopup(r.message || "Auto-book failed. Could not proceed");
    setStatus(r.message || "Auto-book failed");
    return;
  }
  setStatus(r.message || "Auto-book action completed");
  viewBookings();
  refreshFlights();
  loadMandateStatus();
}

async function cheapestRoute() {
  const from = encodeURIComponent(document.getElementById("autoFrom").value.trim());
  const to = encodeURIComponent(document.getElementById("autoTo").value.trim());
  const r = await api(`/cheapest?from=${from}&to=${to}`);
  document.getElementById("cheapestOut").textContent = r.ok
    ? `Cheapest: Flight ${r.flight.id} at ${money(r.flight.currentPrice)}`
    : (r.message || "No route available");
}

async function cancelBooking() {
  const bookingId = document.getElementById("cancelBookingId").value.trim();
  const r = await api("/cancel", "POST", { bookingId });
  setStatus(r.message ? `${r.message}${r.refund ? ` | Refund: ${money(r.refund)}` : ""}` : "Cancel action completed");
  viewBookings();
  refreshFlights();
  loadMandateStatus();
}

async function viewBookings() {
  const r = await api(`/bookings?userId=${currentUserId}`);
  if (!r.ok) return;
  bookingCache = r.bookings || [];
  renderBookingTable();
}

function renderBookingTable() {
  const body = document.getElementById("bookingTable");
  const hint = document.getElementById("bookingSearchHint");
  const search = (document.getElementById("bookingSearch").value || "").trim().toLowerCase();
  body.innerHTML = "";
  const filtered = bookingCache.filter(
    b => !search || String(b.id).toLowerCase().includes(search) || String(b.flightId).toLowerCase().includes(search)
  );

  if (!bookingCache.length) {
    hint.textContent = "No bookings yet. Book a flight to see history here.";
    const tr = document.createElement("tr");
    tr.innerHTML = `<td colspan="5">No booking history available</td>`;
    body.appendChild(tr);
    return;
  }

  if (search && !filtered.length) {
    hint.textContent = `No bookings match "${search}"`;
    const tr = document.createElement("tr");
    tr.innerHTML = `<td colspan="5">No matching bookings found</td>`;
    body.appendChild(tr);
    return;
  }

  hint.textContent = search
    ? `Showing ${filtered.length} result(s) for "${search}"`
    : `Showing all ${filtered.length} booking(s)`;

  filtered.forEach(b => {
      const tr = document.createElement("tr");
      const statusUpper = String(b.status || "").toUpperCase();
      const statusBadge =
        statusUpper === "CONFIRMED"
          ? `<span class="badge badge-confirmed">CONFIRMED</span>`
          : statusUpper === "CANCELLED"
            ? `<span class="badge badge-cancelled">CANCELLED</span>`
            : `<span class="badge">${escapeHtml(statusUpper || "—")}</span>`;
      tr.innerHTML = `<td>${escapeHtml(b.id)}</td><td>${escapeHtml(b.flightId)}</td><td>${statusBadge}</td><td>${money(
        b.pricePaid
      )}</td><td>${money(b.refund)}</td>`;
      body.appendChild(tr);
    });
}

async function loadMandateStatus() {
  const r = await api(`/mandate?userId=${currentUserId}`);
  const live = document.getElementById("mandateLive");
  const body = document.getElementById("mandateHistoryTable");
  body.innerHTML = "";
  if (!r.ok) {
    live.textContent = r.message || "No mandate available";
    if (mandateCountdownTimer) {
      clearInterval(mandateCountdownTimer);
      mandateCountdownTimer = null;
    }
    return;
  }

  const mandate = r.mandate;
  const statusBadgeHtml = (active) =>
    active
      ? `<span class="badge badge-active">ACTIVE</span>`
      : `<span class="badge badge-expired">EXPIRED</span>`;

  const typeBadgeHtml = (type) => {
    const t = String(type || "").toUpperCase();
    if (t === "TRANSACTION") return `<span class="badge badge-transaction">TRANSACTION</span>`;
    if (t === "REFUND") return `<span class="badge badge-refund">REFUND</span>`;
    return `<span class="badge">${escapeHtml(t || "—")}</span>`;
  };

  const renderCountdown = () => {
    const left = Math.max(0, Number(mandate.expiryEpoch) - Math.floor(Date.now() / 1000));
    const mins = Math.floor(left / 60);
    const secs = left % 60;
    const isActive = mandate.active && left > 0;
    live.innerHTML = `UPI: ${escapeHtml(mandate.upiId || "-")} | Limit: ${money(mandate.limitAmount)} | Remaining: ${money(
      mandate.remainingAmount
    )} | Time left: ${String(mins).padStart(2, "0")}:${String(secs).padStart(2, "0")} | Status: ${statusBadgeHtml(
      isActive
    )}`;
    if (!isActive && mandateCountdownTimer) {
      clearInterval(mandateCountdownTimer);
      mandateCountdownTimer = null;
    }
  };

  renderCountdown();
  if (mandateCountdownTimer) clearInterval(mandateCountdownTimer);
  mandateCountdownTimer = setInterval(renderCountdown, 1000);

  const history = (r.history || [])
    .slice()
    .sort((a, b) => Number(b.createdAt) - Number(a.createdAt))
    .slice(0, 100);

  if (!history.length) {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td colspan="5">No transactions or refunds yet</td>`;
    body.appendChild(tr);
    return;
  }

  history.forEach(entry => {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td>${typeBadgeHtml(entry.type)}</td><td>${escapeHtml(entry.bookingId || "-")}</td><td>${money(
      entry.amount
    )}</td><td>${money(entry.balanceAfter)}</td><td>${new Date(Number(entry.createdAt) * 1000).toLocaleString()}</td>`;
    body.appendChild(tr);
  });
}

function logoutUser() {
  clearUserSession();
  window.location.href = "index.html";
}

refreshFlights();
viewBookings();
loadMandateStatus();
