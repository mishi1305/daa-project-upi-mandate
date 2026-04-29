# Project Description

**FlexiP₹ice** is a full-stack travel booking platform that implements dynamic pricing, automated booking, and role-based access control. The backend is built in C++ using a custom HTTP server with Winsock, handling user authentication, flight management, UPI mandate-based payments, and booking workflows with file-based persistence.

The system features real-time price fluctuation using configurable multipliers, a greedy-based auto-booking mechanism to select the cheapest available flight within user constraints, and secure admin controls for managing inventory and pricing. The frontend is developed using HTML, CSS, and JavaScript, providing separate dashboards for users and administrators to interact with the system seamlessly.

# How to Run

## 1) Stop any old backend if stuck
taskkill /F /IM server.exe

## 2) Compile
g++ .\backend\main.cpp -std=c++17 -O2 -lws2_32 -o .\backend\server.exe

## 3) Run backend
.\backend\server.exe

## 3.5) If backend says bind/permission error
taskkill /F /IM server.exe

## 4) Frontend
go to the frontend folder and open index.html after running the backend

Then use the multi-page flow:

Home -> User -> user-auth.html -> user-dashboard.html
Home -> Admin -> admin-login.html -> admin-dashboard.html

## Default Admin

- Email: `admin@flexiprice.com`
- Password: `admin123`
