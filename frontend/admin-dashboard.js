if (localStorage.getItem("adminLoggedIn") !== "1") {
  window.location.href = "admin-login.html";
}

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
  setStatus("Flights refreshed");
}

async function addFlight() {
  const payload = {
    from: document.getElementById("fromCity").value.trim(),
    to: document.getElementById("toCity").value.trim(),
    departure: document.getElementById("depTime").value.trim(),
    basePrice: Number(document.getElementById("basePrice").value),
    seats: Number(document.getElementById("seats").value)
  };
  const r = await api("/admin/flights", "POST", payload);
  setStatus(r.message || "Add flight completed");
  refreshFlights();
}

async function updatePricing() {
  const payload = {
    minMultiplier: Number(document.getElementById("minMul").value),
    maxMultiplier: Number(document.getElementById("maxMul").value)
  };
  const r = await api("/admin/pricing", "POST", payload);
  setStatus(r.message || "Pricing update completed");
}

function logoutAdmin() {
  localStorage.removeItem("adminLoggedIn");
  window.location.href = "index.html";
}

refreshFlights();
