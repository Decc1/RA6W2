#ifndef HISTORY_STORAGE_H
#define HISTORY_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "history_service.h"

/*
 * 历史记录存储接口。
 *
 * 当前实现为 RAM 环形缓冲区，用于无开发板阶段的业务联调和自检；断电后数据会丢失。
 * 后续接入真实 Flash 时，只替换 history_storage.c 的内部实现，history_service、BLE
 * 和 Wi-Fi 上层仍然使用本文件中的接口。
 */

/* 初始化或清空 RAM 模拟存储。 */
void history_storage_init(void);

/* 追加一条已完成的 15 分钟聚合记录。满容量后自动覆盖最旧记录。 */
bool history_storage_append(history_record_t const * p_record);

/* 获取当前可读取的记录数量。 */
bool history_storage_get_count(uint16_t * p_count);

/*
 * 按时间先后读取记录：index = 0 表示最旧记录，index = count - 1 表示最新记录。
 * 索引越界或参数为空时返回 false。
 */
bool history_storage_read(uint16_t index, history_record_t * p_record);

/* 清空所有历史记录；未来对应恢复出厂时删除本地历史数据的操作。 */
void history_storage_clear(void);

/* 无硬件阶段的 RAM 存储单元自检。 */
bool history_storage_self_test(void);

#endif /* HISTORY_STORAGE_H */
