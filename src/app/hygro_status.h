#ifndef HYGRO_STATUS_H
#define HYGRO_STATUS_H

#include <stdbool.h>

/* 作用: 定义驱动、服务和应用层统一使用的内部错误码。 */
typedef enum e_hygro_status
{
    HYGRO_STATUS_OK = 0,
    HYGRO_STATUS_INVALID_ARGUMENT,
    HYGRO_STATUS_NOT_READY,
    HYGRO_STATUS_IO_ERROR,
    HYGRO_STATUS_CRC_ERROR,
    HYGRO_STATUS_BUS_NACK,
    HYGRO_STATUS_BUS_TIMEOUT,
    HYGRO_STATUS_BUS_ERROR,
    HYGRO_STATUS_PROTOCOL_ERROR,
    HYGRO_STATUS_STORAGE_ERROR,
    HYGRO_STATUS_NETWORK_ERROR,
    HYGRO_STATUS_TLS_ERROR,
    HYGRO_STATUS_TIMEOUT,
    HYGRO_STATUS_BUSY,
    HYGRO_STATUS_UNSUPPORTED,
} hygro_status_t;

/* 作用: 判断状态是否表示操作成功。 */
bool hygro_status_is_ok(hygro_status_t status);

/* 作用: 返回稳定的错误码名称，供日志和诊断接口输出。 */
char const * hygro_status_name(hygro_status_t status);

/* 作用: 验证统一错误码名称和成功判断逻辑。 */
bool hygro_status_self_test(void);

#endif /* HYGRO_STATUS_H */
