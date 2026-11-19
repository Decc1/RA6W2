# HYGRO-RA6W2 BLE GATT 协议草案 V0.1

## 1. 目的和边界

本协议用于手机在设备附近、并已通过 BLE 连接设备时读取温湿度数据，及向设备发送控制命令。

- 设备：BLE GATT Server（服务端）
- 手机 App：BLE GATT Client（客户端）
- 本文是无开发板阶段的接口草案，不代表 BLE 广播、配对、Notify 或 Indicate 已经在板端实现。
- 多字节整数统一采用小端序（Little Endian）。温湿度使用整数缩放值，不使用浮点数。

## 2. 服务与 Characteristic

| 项目 | UUID | 属性 | 方向 | 用途 |
| --- | --- | --- | --- | --- |
| 自定义温湿度服务 | `A0E0F010-6D49-4D80-8F32-AB0D7D000000` | - | - | 所有下列 Characteristic 的父服务 |
| 实时测量值 | `A0E0F011-6D49-4D80-8F32-AB0D7D000000` | Read、Notify | 设备 → App | 读取或每 10 秒 Notify 最新温湿度 |
| 控制命令 | `A0E0F012-6D49-4D80-8F32-AB0D7D000000` | Write | App → 设备 | App 写入命令帧 |
| 命令结果 | `A0E0F013-6D49-4D80-8F32-AB0D7D000000` | Read、Indicate | 设备 → App | 返回执行结果，App 必须订阅 Indicate |

> UUID 目前为项目草案。App 合同、量产固件和通信接口控制文档必须使用同一组正式 UUID；量产后不可随意更改。

## 3. 实时测量值 Characteristic

长度固定为 12 字节：

| 字节偏移 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | version | `uint8` | 固定为 `0x01` |
| 1 | flags | `uint8` | bit0 = 1 表示本帧温湿度有效 |
| 2–3 | temp_cc | `int16 LE` | 温度，单位 0.01°C；`2350` 表示 23.50°C |
| 4–5 | hum_crh | `uint16 LE` | 湿度，单位 0.01%RH；`4550` 表示 45.50%RH |
| 6–7 | bat_mv | `uint16 LE` | 电池电压，单位 mV；`3850` 表示 3.850 V |
| 8–11 | sample_sequence | `uint32 LE` | 从上电开始单调递增的采样序号 |

App 连接后应先 Read 一次获得当前值，再写 CCCD 打开 Notify。BLE 保持连接时，设备每 10 秒 Notify 一次；若数据无效，则不发送虚假的有效温湿度。

示例（十六进制）：`01 01 2E 09 C6 11 0A 0F 07 00 00 00`

表示：23.50°C、45.50%RH、3850 mV、采样序号 7。

## 4. 控制命令 Characteristic

App 使用 GATT Write 写入以下帧。每次发送的 `command_id` 由 App 递增生成，设备回复时原样带回，供 App 匹配请求和结果。

| 字节偏移 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | version | `uint8` | 固定 `0x01` |
| 1 | command_code | `uint8` | 命令码 |
| 2–3 | command_id | `uint16 LE` | App 请求编号 |
| 4 | payload_length | `uint8` | 后续 payload 的字节长度 |
| 5… | payload | bytes | 命令参数；可为 0 字节 |

第一阶段定义：

| 命令码 | 名称 | payload | 当前计划 |
| --- | --- | --- | --- |
| `0x01` | `GET_DEVICE_INFO` | 空 | 返回软件版本、协议版本等；待接入实际版本信息 |
| `0x02` | `GET_LATEST_MEASUREMENT` | 空 | 返回与实时测量值相同的 12 字节载荷 |
| `0x10` | `WIFI_PROVISION_BEGIN` | `transaction_id(2) + ssid_length(1) + password_length(1)` | 开始一次 BLE 配网事务 |
| `0x11` | `WIFI_PROVISION_CHUNK` | `transaction_id(2) + offset(1) + chunk_length(1) + data` | 按顺序写入 SSID 和密码片段 |
| `0x12` | `WIFI_PROVISION_COMMIT` | `transaction_id(2)` | 校验数据完整后保存凭据并连接 Wi-Fi |
| `0x13` | `WIFI_PROVISION_ABORT` | `transaction_id(2)` | 取消本次配网并清除 RAM 暂存凭据 |
| `0x14` | `CLEAR_WIFI_CREDENTIALS` | 空 | 清除已保存 Wi-Fi 凭据预留 |
| `0x20` | `START_OTA` | 后续定义 | OTA 流程预留 |
| `0x30` | `FACTORY_RESET` | 空 | 恢复出厂预留；正式实现前须增加二次确认 |

`GET_LATEST_MEASUREMENT` 示例：`01 02 34 12 00`，其中 `0x1234` 是 command_id。

### 4.1 BLE 配网规则

1. 仅在设备首次未配网、用户主动进入配网模式，或恢复出厂后，允许执行配网命令。
2. 设备必须确认当前 BLE 链路已加密且已绑定；否则所有配网命令返回 `NOT_AUTHORIZED`。
3. 第一版仅支持 2.4 GHz WPA2/WPA3-Personal：SSID 长度 1–32 字节，密码长度 8–63 字节。
4. `CHUNK` 的 `data` 是 `SSID` 与 `password` 顺序拼接后的分片；`offset` 必须从 0 连续递增，设备不接受乱序分片。
5. App 每发送一片后必须等待同一 `command_id` 的 Indicate 成功回复，再发送下一片；失败或断连时应发送 `ABORT` 或重新从 `BEGIN` 开始。
6. `COMMIT` 前，设备检查是否已收齐全部字节；提交给 Wi-Fi 服务后立即清除 RAM 中的暂存密码。
7. 密码不得写入串口日志、MQTT、HTTP 请求或普通调试输出。

### 4.2 固件处理路径

真实 BLE 协议栈收到 GATT Write 后，将原始数据交给 `device_command_handle_ble_write()`：

```text
GATT Write
  → 控制帧解析
  → 命令分发
  → 温湿度服务 / Wi-Fi 配网状态机
  → 命令结果帧编码
  → GATT Indicate
```

其中 Wi-Fi 配网状态机目前只完成参数检查与 RAM 暂存。Wi-Fi 服务层已定义“安全保存凭据 → 异步连接路由器 → 获取 IP”的状态接口；真实项目仍需接入 RA6W2 Wi-Fi 驱动和加密存储，才可向 App 返回配网成功。

## 5. 命令结果 Characteristic

命令执行完成后，设备使用 GATT Indicate 发送回复。使用 Indicate 而非普通 Notify，是为了让 BLE 链路确认 App 已收到“设置成功/失败”的结果。

| 字节偏移 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | version | `uint8` | 固定 `0x01` |
| 1 | command_code | `uint8` | 与请求一致 |
| 2–3 | command_id | `uint16 LE` | 与请求一致 |
| 4 | status | `uint8` | 命令结果状态码 |
| 5 | payload_length | `uint8` | 后续 payload 长度 |
| 6… | payload | bytes | 可选结果数据 |

状态码：

| 值 | 名称 | 含义 |
| --- | --- | --- |
| `0x00` | OK | 命令已成功执行 |
| `0x01` | INVALID_FRAME | 长度、版本或帧结构不正确 |
| `0x02` | UNSUPPORTED_COMMAND | 当前固件不支持该命令 |
| `0x03` | INVALID_PAYLOAD | 参数不合法 |
| `0x04` | BUSY | 设备正在处理其他互斥操作 |
| `0x05` | NOT_READY | 依赖模块未就绪，例如无有效传感器数据 |
| `0x06` | FAILED | 执行失败 |
| `0x07` | NOT_AUTHORIZED | 未完成绑定/鉴权 |
| `0x08` | TIMEOUT | 操作超时 |

## 6. App 联调规则

1. App 连接后发现自定义服务。
2. 对“实时测量值”执行 Read，并订阅 Notify。
3. 对“命令结果”订阅 Indicate。
4. App 发送控制命令后，等待同一个 `command_id` 的回复；推荐超时 10 秒。
5. 超时重试时必须沿用原 `command_id`。正式固件需要按 `command_id` 去重，避免同一命令重复执行。
6. 每个产品第一版仅允许一个绑定用户；具体配对、绑定和密钥策略在安全设计阶段另行明确。

## 7. 当前实现状态

- 已完成：实时测量值 12 字节编码、控制命令帧校验、命令回复帧编码、无硬件自检。
- 尚未完成：真实 BLE 协议栈接入、广播、GATT 数据库注册、Notify、Indicate、配对绑定、Wi-Fi 配网、OTA。
