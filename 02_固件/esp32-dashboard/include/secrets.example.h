#pragma once
// 复制本文件为 secrets.h 并填写

#define WIFI_SSID     "your-wifi"
#define WIFI_PASSWORD "your-password"

// 打印机屏幕：设置 -> 网络 -> 局域网模式，可以看到 IP 和访问码
#define BAMBU_IP          "192.168.1.100"
#define BAMBU_SERIAL      "0000000000000000"
#define BAMBU_ACCESS_CODE "12345678"

// 第二台打印机（序列号可用 openssl s_client -connect <IP>:8883 查看证书 CN）
#define BAMBU2_IP          "192.168.1.101"
#define BAMBU2_SERIAL      "0000000000000000"
#define BAMBU2_ACCESS_CODE "12345678"

// 浏览器登录 cursor.com -> 开发者工具 -> Application -> Cookies
// 复制 WorkosCursorSessionToken 的值（形如 user_xxx%3A%3AeyJ...）
#define CURSOR_SESSION_TOKEN ""
