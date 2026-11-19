#include "history_storage.h"

#include <string.h>

#include "../config/app_config.h"

/* RAM 模拟后端：以环形队列方式保存已完成的历史聚合记录。 */
typedef struct st_history_storage_context
{
    history_record_t records[APP_HISTORY_RAM_CAPACITY];
    uint16_t         oldest_index;
    uint16_t         record_count;
} history_storage_context_t;

static history_storage_context_t g_history_storage;

/* 将按时间顺序的逻辑索引转换为 RAM 环形队列中的物理槽位。 */
/* 作用：把按时间排序的逻辑索引转换为 RAM 环形缓冲区中的物理槽位。 */
static uint16_t history_storage_slot_from_logical_index(uint16_t logical_index)
{
    return (uint16_t) ((g_history_storage.oldest_index + logical_index) % APP_HISTORY_RAM_CAPACITY);
}

/* 作用：初始化 RAM 历史记录缓冲区，清除所有未持久化的历史数据。 */
void history_storage_init(void)
{
    (void) memset(&g_history_storage, 0, sizeof(g_history_storage));
}

/* 作用：追加一条历史记录；缓冲区已满时覆盖最旧记录。 */
bool history_storage_append(history_record_t const * p_record)
{
    uint16_t write_index;

    if (NULL == p_record)
    {
        return false;
    }

    if (g_history_storage.record_count < APP_HISTORY_RAM_CAPACITY)
    {
        write_index = history_storage_slot_from_logical_index(g_history_storage.record_count);
        g_history_storage.record_count++;
    }
    else
    {
        /* 缓冲区已满：覆盖最旧记录，并将“最旧”向前移动一个槽位。 */
        write_index = g_history_storage.oldest_index;
        g_history_storage.oldest_index = (uint16_t) ((g_history_storage.oldest_index + 1U) % APP_HISTORY_RAM_CAPACITY);
    }

    g_history_storage.records[write_index] = *p_record;
    return true;
}

/* 作用：返回当前可读取的历史记录条数。 */
bool history_storage_get_count(uint16_t * p_count)
{
    if (NULL == p_count)
    {
        return false;
    }

    *p_count = g_history_storage.record_count;
    return true;
}

/* 作用：按从最旧到最新的逻辑顺序读取一条历史记录。 */
bool history_storage_read(uint16_t index, history_record_t * p_record)
{
    if ((NULL == p_record) || (index >= g_history_storage.record_count))
    {
        return false;
    }

    *p_record = g_history_storage.records[history_storage_slot_from_logical_index(index)];
    return true;
}

/* 作用：清空全部历史记录；未来可映射为恢复出厂时的 Flash 擦除。 */
void history_storage_clear(void)
{
    history_storage_init();
}

/* 作用：验证 RAM 环形缓冲的追加、读取及满容量覆盖顺序。 */
bool history_storage_self_test(void)
{
    history_record_t record = {0};
    history_record_t readback = {0};
    uint16_t         record_count;
    uint16_t         index;

    history_storage_init();

    record.window_start_uptime_s = 0U;
    record.sample_count          = 90U;
    if (!history_storage_append(&record))
    {
        return false;
    }

    record.window_start_uptime_s = APP_HISTORY_WINDOW_MS / 1000U;
    if (!history_storage_append(&record) ||
        !history_storage_get_count(&record_count) ||
        (2U != record_count) ||
        !history_storage_read(0U, &readback) ||
        (0U != readback.window_start_uptime_s) ||
        !history_storage_read(1U, &readback) ||
        ((APP_HISTORY_WINDOW_MS / 1000U) != readback.window_start_uptime_s))
    {
        return false;
    }

    /* 验证容量满时，读取顺序仍保持“最旧到最新”。 */
    history_storage_clear();
    for (index = 0U; index <= APP_HISTORY_RAM_CAPACITY; index++)
    {
        record.window_start_uptime_s = index;
        if (!history_storage_append(&record))
        {
            return false;
        }
    }

    if (!history_storage_get_count(&record_count) ||
        (APP_HISTORY_RAM_CAPACITY != record_count) ||
        !history_storage_read(0U, &readback) ||
        (1U != readback.window_start_uptime_s) ||
        !history_storage_read((uint16_t) (APP_HISTORY_RAM_CAPACITY - 1U), &readback) ||
        (APP_HISTORY_RAM_CAPACITY != readback.window_start_uptime_s))
    {
        return false;
    }

    history_storage_clear();
    return true;
}
