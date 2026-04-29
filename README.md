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
