# ESP32-S3 拓竹看板

ESP32-S3-N16R8 驱动 4.0 寸 ST7796 电阻触摸屏，显示两台拓竹打印机、Cursor 用量、重庆天气和系统设置。

左右滑动换页。只有当前这一页会联网，划走就断开，避免几路加密连接同时占用内存。

## 配置

把 `include/secrets.example.h` 复制成 `include/secrets.h` 再填写。`secrets.h` 已在 `.gitignore` 里，不要提交。

城市和界面上的打印机名字在 `include/config.h`。

打印机不用打开「仅局域网」，否则手机 App 会找不到设备。

## 编译

用 [PlatformIO](https://platformio.org/) 打开本目录，连接开发板 USB 后上传。

板子是 ESP32-S3-N16R8。`platformio.ini` 已选择 `esp32-s3-devkitc-1`、16MB Flash 和 OPI PSRAM。

连上 WiFi 后，浏览器打开开发板的 IP，或 `http://esp32dash.local/`。
