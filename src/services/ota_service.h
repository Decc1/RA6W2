#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/* 固件版本号和 SHA-256 摘要长度。URL 仅暂存于 RAM，不写入普通日志。 */
#define OTA_SERVICE_VERSION_MAX_LENGTH     (32U)
#define OTA_SERVICE_URL_MAX_LENGTH         (160U)
#define OTA_SERVICE_SHA256_LENGTH          (32U)

typedef enum e_ota_service_state
{
    OTA_SERVICE_STATE_UNINITIALIZED = 0,
    OTA_SERVICE_STATE_IDLE,
    OTA_SERVICE_STATE_DOWNLOADING,
    OTA_SERVICE_STATE_VERIFYING,
    OTA_SERVICE_STATE_PENDING_BOOT,
    OTA_SERVICE_STATE_TRIAL_BOOT,
    OTA_SERVICE_STATE_CONFIRMED,
    OTA_SERVICE_STATE_FAILED,
    OTA_SERVICE_STATE_ROLLBACK_REQUIRED,
} ota_service_state_t;

typedef struct st_ota_manifest
{
    char     version[OTA_SERVICE_VERSION_MAX_LENGTH + 1U];
    char     url[OTA_SERVICE_URL_MAX_LENGTH + 1U];
    uint8_t  sha256[OTA_SERVICE_SHA256_LENGTH];
    uint32_t image_size_bytes;
} ota_manifest_t;

/* 后续由 HTTPS 下载、外部 Flash、Bootloader 模块实现的适配接口。 */
typedef struct st_ota_service_platform_ops
{
    bool (* start_download)(ota_manifest_t const * p_manifest);
    bool (* mark_pending_boot)(ota_manifest_t const * p_manifest);
    bool (* confirm_running_image)(void);
    bool (* request_rollback)(void);
} ota_service_platform_ops_t;

void ota_service_init(ota_service_platform_ops_t const * p_platform_ops);
bool ota_service_request_update(ota_manifest_t const * p_manifest, bool wifi_connected);
void ota_service_notify_download_result(bool download_success, bool sha256_matches);
/* Bootloader 持久化标志表明当前为新固件试运行时，由应用调用。 */
void ota_service_notify_trial_boot(void);
bool ota_service_confirm_running_image(bool application_healthy);
ota_service_state_t ota_service_get_state(void);
bool ota_service_self_test(void);

#endif /* OTA_SERVICE_H */
