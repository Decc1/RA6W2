# HYGRO-RA6W2 Wi-Fi 服务状态机草案 V0.1

## 状态迁移

```text
未配网
  → BLE 配网提交
  → 已安全保存凭据
  → 正在连接路由器
  → 已关联，等待 DHCP/IP
  → 已联网

任一步失败 → 连接失败
```

## 模块边界

- `wifi_provisioning`：只处理 BLE 分片、校验和 RAM 中的临时凭据。
- `wifi_service`：请求安全保存凭据并启动连接，维护联网状态。
- 未来 RA6W2 Wi-Fi 适配层：实现加密存储、连接路由器，以及将 WLAN 事件回调为 `wifi_service_notify_link_result()` 和 `wifi_service_notify_ip_acquired()`。
- MQTT 服务只能在 `WIFI_SERVICE_STATE_CONNECTED` 后启动。

## 当前限制

目前未接入真实 RA6W2 Wi-Fi 驱动、Flash 加密存储、DHCP、MQTT 或重试定时器。因此配网 `COMMIT` 仍会返回 `NOT_READY`，不会错误地报告联网成功。
