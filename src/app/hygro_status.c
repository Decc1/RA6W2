#include "hygro_status.h"

#include <string.h>

/* 作用: 判断状态是否表示操作成功。 */
bool hygro_status_is_ok(hygro_status_t status)
{
    return HYGRO_STATUS_OK == status;
}

/* 作用: 返回稳定的错误码名称，供日志和诊断接口输出。 */
char const * hygro_status_name(hygro_status_t status)
{
    switch (status)
    {
        case HYGRO_STATUS_OK:               return "OK";
        case HYGRO_STATUS_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case HYGRO_STATUS_NOT_READY:        return "NOT_READY";
        case HYGRO_STATUS_IO_ERROR:         return "IO_ERROR";
        case HYGRO_STATUS_CRC_ERROR:        return "CRC_ERROR";
        case HYGRO_STATUS_BUS_NACK:         return "BUS_NACK";
        case HYGRO_STATUS_BUS_TIMEOUT:      return "BUS_TIMEOUT";
        case HYGRO_STATUS_BUS_ERROR:        return "BUS_ERROR";
        case HYGRO_STATUS_PROTOCOL_ERROR:   return "PROTOCOL_ERROR";
        case HYGRO_STATUS_STORAGE_ERROR:    return "STORAGE_ERROR";
        case HYGRO_STATUS_NETWORK_ERROR:    return "NETWORK_ERROR";
        case HYGRO_STATUS_TLS_ERROR:        return "TLS_ERROR";
        case HYGRO_STATUS_TIMEOUT:          return "TIMEOUT";
        case HYGRO_STATUS_BUSY:             return "BUSY";
        case HYGRO_STATUS_UNSUPPORTED:      return "UNSUPPORTED";
        default:                             return "UNKNOWN";
    }
}

/* 作用: 验证统一错误码名称和成功判断逻辑。 */
bool hygro_status_self_test(void)
{
    return hygro_status_is_ok(HYGRO_STATUS_OK) &&
           !hygro_status_is_ok(HYGRO_STATUS_BUS_TIMEOUT) &&
           (0 == strcmp("OK", hygro_status_name(HYGRO_STATUS_OK))) &&
           (0 == strcmp("UNKNOWN", hygro_status_name((hygro_status_t) 99)));
}
