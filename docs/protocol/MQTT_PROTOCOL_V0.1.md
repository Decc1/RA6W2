# HYGRO-RA6W2 MQTT 协议草案 V0.1

## 1. 连接方式

设备仅在 Wi-Fi 已获取 IP 后建立 MQTT over TLS 连接。第一版采用短连接：连接、订阅命令、上报数据、接收确认后断开，降低平均功耗。

## 2. Topic

设备 ID 由生产或首次绑定流程分配，格式限制为字母、数字、下划线、连字符，最长 32 字节。

| Topic | 方向 | QoS | retain | 用途 |
| --- | --- | --- | --- | --- |
| `hygro/v1/{device_id}/telemetry` | 设备 → 云端 | 1 | false | 温湿度、电池和采样序号上报 |
| `hygro/v1/{device_id}/command` | 云端 → 设备 | 1 | false | 云端缓存的控制命令 |
| `hygro/v1/{device_id}/reply` | 设备 → 云端 | 1 | false | 命令执行结果 |

云端收到 telemetry 后保存历史数据，并提供 HTTPS/WebSocket 给 App；App 不直接连接设备的 MQTT Broker。

## 3. Telemetry JSON

```json
{"v":1,"seq":7,"temp_cc":2350,"hum_crh":4550,"bat_mv":3850,"uptime_ms":10000}
```

- `temp_cc=2350` 表示 23.50°C。
- `hum_crh=4550` 表示 45.50%RH。
- 服务端记录接收时间，设备在完成 NTP 校时前不上传不可信的绝对时间。

## 4. Command 与 Reply

云端命令至少包括：`id`、`cmd`、`args`；其中 `id` 全局唯一，用于去重和匹配回复。设备执行后发布：

```json
{"v":1,"id":"cmd_001","status":0}
```

`status=0` 表示成功，其他值与 BLE 命令回复状态码一致。设备每次联网后先订阅 `command` Topic，再上报 telemetry。云端必须通过 MQTT 持久会话或自身的待执行命令队列保存离线命令；仅设置 QoS 1 并不能保证短连接设备下次上线一定收到旧命令。

## 5. 当前实现状态

已完成 Topic 和 JSON 编码函数。尚未实现 TLS 证书、MQTT 客户端、Broker 鉴权、命令 JSON 解析、离线缓存和 15 分钟调度。
