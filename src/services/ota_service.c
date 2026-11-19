#include "ota_service.h"

#include <string.h>

typedef struct st_ota_service_context
{
    ota_service_platform_ops_t ops;
    ota_manifest_t             manifest;
    ota_service_state_t        state;
    bool                       ready;
} ota_service_context_t;

static ota_service_context_t g_ota;

/* 作用：检查 OTA 清单是否包含版本、下载地址和非零固件大小。 */
static bool ota_manifest_valid(ota_manifest_t const * p_manifest)
{
    return (NULL != p_manifest) && ('\0' != p_manifest->version[0]) &&
           ('\0' != p_manifest->url[0]) && (0U != p_manifest->image_size_bytes);
}

/* 作用：初始化 OTA 状态机，并绑定下载、确认和回滚的平台适配接口。 */
void ota_service_init(ota_service_platform_ops_t const * p_platform_ops)
{
    (void) memset(&g_ota, 0, sizeof(g_ota));
    if (NULL != p_platform_ops)
    {
        g_ota.ops = *p_platform_ops;
        g_ota.ready = (NULL != g_ota.ops.start_download) &&
                      (NULL != g_ota.ops.mark_pending_boot) &&
                      (NULL != g_ota.ops.confirm_running_image) &&
                      (NULL != g_ota.ops.request_rollback);
    }
    g_ota.state = OTA_SERVICE_STATE_IDLE;
}

/* 作用：在 Wi-Fi 已连接且服务空闲时校验升级清单并启动固件下载。 */
bool ota_service_request_update(ota_manifest_t const * p_manifest, bool wifi_connected)
{
    if (!g_ota.ready || !wifi_connected || (OTA_SERVICE_STATE_IDLE != g_ota.state) ||
        !ota_manifest_valid(p_manifest))
    {
        return false;
    }
    g_ota.manifest = *p_manifest;
    if (!g_ota.ops.start_download(&g_ota.manifest))
    {
        g_ota.state = OTA_SERVICE_STATE_FAILED;
        return false;
    }
    g_ota.state = OTA_SERVICE_STATE_DOWNLOADING;
    return true;
}

/* 作用：接收下载和 SHA-256 校验结果，决定是否标记新镜像待重启试运行。 */
void ota_service_notify_download_result(bool download_success, bool sha256_matches)
{
    if (OTA_SERVICE_STATE_DOWNLOADING != g_ota.state)
    {
        return;
    }
    if (!download_success || !sha256_matches)
    {
        g_ota.state = OTA_SERVICE_STATE_FAILED;
        return;
    }
    g_ota.state = OTA_SERVICE_STATE_VERIFYING;
    if (!g_ota.ops.mark_pending_boot(&g_ota.manifest))
    {
        g_ota.state = OTA_SERVICE_STATE_FAILED;
        return;
    }
    g_ota.state = OTA_SERVICE_STATE_PENDING_BOOT;
}

/* 作用：由启动适配层通知当前已从待确认的新固件镜像启动。 */
void ota_service_notify_trial_boot(void)
{
    /*
     * 重启后 RAM 状态已丢失；只有 Bootloader 的持久化升级标志确认
     * “本次为新镜像试运行”时，平台适配层才允许调用本函数。
     */
    g_ota.state = OTA_SERVICE_STATE_TRIAL_BOOT;
}

/* 作用：应用自检成功时确认新镜像；失败时请求 Bootloader 回滚。 */
bool ota_service_confirm_running_image(bool application_healthy)
{
    if (OTA_SERVICE_STATE_TRIAL_BOOT != g_ota.state)
    {
        return false;
    }
    if (application_healthy && g_ota.ops.confirm_running_image())
    {
        g_ota.state = OTA_SERVICE_STATE_CONFIRMED;
        return true;
    }
    g_ota.state = OTA_SERVICE_STATE_ROLLBACK_REQUIRED;
    (void) g_ota.ops.request_rollback();
    return false;
}

/* 作用：返回 OTA 状态机当前状态，供上层诊断或对 App 回报。 */
ota_service_state_t ota_service_get_state(void)
{
    return g_ota.state;
}

/* 作用：OTA 自检使用的模拟下载启动回调。 */
static bool ota_test_start(ota_manifest_t const * p_manifest)
{
    return ota_manifest_valid(p_manifest);
}

/* 作用：OTA 自检使用的模拟“标记待启动镜像”回调。 */
static bool ota_test_manifest(ota_manifest_t const * p_manifest)
{
    (void) p_manifest;
    return true;
}

/* 作用：OTA 自检使用的无参数成功回调，模拟确认或回滚请求。 */
static bool ota_test_no_argument(void)
{
    return true;
}

/* 作用：验证 OTA 从请求下载、试运行到确认新镜像的正常状态流转。 */
bool ota_service_self_test(void)
{
    static ota_service_platform_ops_t const ops =
    {
        .start_download = ota_test_start,
        .mark_pending_boot = ota_test_manifest,
        .confirm_running_image = ota_test_no_argument,
        .request_rollback = ota_test_no_argument,
    };
    ota_manifest_t manifest = {"0.2.0", "https://example.invalid/fw.bin", {0}, 1024U};

    ota_service_init(&ops);
    if (!ota_service_request_update(&manifest, true))
    {
        return false;
    }
    ota_service_notify_download_result(true, true);
    ota_service_notify_trial_boot();
    if (!ota_service_confirm_running_image(true))
    {
        return false;
    }
    ota_service_init(NULL);
    return (OTA_SERVICE_STATE_IDLE == ota_service_get_state());
}
