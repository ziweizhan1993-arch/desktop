#pragma once

#include <Arduino.h>

// 同一时间只允许一个 TLS 握手。两台打印机的 MQTT 和天气、Cursor 的 HTTPS
// 同时握手时，内部内存不够，全部会失败：打印机离线，HTTP 返回 -1。
void tlsInit();
void tlsLock();
void tlsUnlock();

void noteNetOk();
uint32_t lastNetOkMs();

// 内部堆最大连续块够不够再开一条 TLS
bool tlsHeapOk();
size_t tlsLargestBlock();

struct TlsLock {
  TlsLock() { tlsLock(); }
  ~TlsLock() { tlsUnlock(); }
};
