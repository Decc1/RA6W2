#include "ota_service.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

typedef struct st_ota_service_context
{
    ota_service_platform_ops_t ops;
    ota_manifest_t             manifest;
    ota_service_state_t        state;
    bool                       ready;
} ota_service_context_t;

static ota_service_context_t g_ota;

/* 作用: 检查 OTA 清单是否包含版本、下载地址和非零固件大小。 */
static bool ota_manifest_valid(ota_manifest_t const * p_manifest)
{
    return (NULL != p_manifest) && ('\0' != p_manifest->version[0]) &&
           ('\0' != p_manifest->url[0]) && (0U != p_manifest->image_size_bytes);
}

/* 作用: 初始化 OTA 状态机，并绑定下载、确认和回滚的平台适配接口。 */
void ota_service_init(ota_service_platform_ops_t const * p_platform_ops)
{
    taskENTER_CRITICAL();
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
    taskEXIT_CRITICAL();
}

/* 作用: 在 Wi-Fi 已连接且服务空闲时校验升级清单并启动固件下载。 */
bool ota_service_request_update(ota_manifest_t const * p_manifest, bool wifi_connected)
{
    ota_service_platform_ops_t ops;
    ota_manifest_t             manifest;

    if (!wifi_connected || !ota_manifest_valid(p_manifest))
    {
        return false;
    }

    taskENTER_CRITICAL();
    if (!g_ota.ready || (OTA_SERVICE_STATE_IDLE != g_ota.state))
    {
        taskEXIT_CRITICAL();
        return false;
    }
    g_ota.manifest = *p_manifest;
    g_ota.state = OTA_SERVICE_STATE_DOWNLOADING;
    ops = g_ota.ops;
    manifest = g_ota.manifest;
    taskEXIT_CRITICAL();

    if (!ops.start_download(&manifest))
    {
        taskENTER_CRITICAL();
        g_ota.state = OTA_SERVICE_STATE_FAILED;
        taskEXIT_CRITICAL();
        return false;
    }

    return true;
}

/* 作用: 接收下载和 SHA-256 校验结果，决定是否标记新镜像待重启试运行。 */
void ota_service_notify_download_result(bool download_success, bool sha256_matches)
{
    ota_manifest_t             manifest;
    ota_service_platform_ops_t ops;

    taskENTER_CRITICAL();
    if (OTA_SERVICE_STATE_DOWNLOADING != g_ota.state)
    {
        taskEXIT_CRITICAL();
        return;
    }
    if (!download_success || !sha256_matches)
    {
        g_ota.state = OTA_SERVICE_STATE_FAILED;
        taskEXIT_CRITICAL();
        return;
    }
    g_ota.state = OTA_SERVICE_STATE_VERIFYING;
    manifest = g_ota.manifest;
    ops = g_ota.ops;
    taskEXIT_CRITICAL();

    if ((NULL == ops.mark_pending_boot) || !ops.mark_pending_boot(&manifest))
    {
        taskENTER_CRITICAL();
        g_ota.state = OTA_SERVICE_STATE_FAILED;
        taskEXIT_CRITICAL();
        return;
    }

    taskENTER_CRITICAL();
    g_ota.state = OTA_SERVICE_STATE_PENDING_BOOT;
    taskEXIT_CRITICAL();
}

/* 作用: 由启动适配层通知当前已从待确认的新固件镜像启动。 */
void ota_service_notify_trial_boot(void)
{
    taskENTER_CRITICAL();
    if (g_ota.ready && (OTA_SERVICE_STATE_PENDING_BOOT == g_ota.state))
    {
        g_ota.state = OTA_SERVICE_STATE_TRIAL_BOOT;
    }
    taskEXIT_CRITICAL();
}

/* 作用: 应用自检成功时确认新镜像；失败时请求 Bootloader 回滚。 */
bool ota_service_confirm_running_image(bool application_healthy)
{
    ota_service_platform_ops_t ops;

    taskENTER_CRITICAL();
    if (OTA_SERVICE_STATE_TRIAL_BOOT != g_ota.state)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    ops = g_ota.ops;
    taskEXIT_CRITICAL();

    if (application_healthy && (NULL != ops.confirm_running_image) &&
        ops.confirm_running_image())
    {
        taskENTER_CRITICAL();
        g_ota.state = OTA_SERVICE_STATE_CONFIRMED;
        taskEXIT_CRITICAL();
        return true;
    }

    taskENTER_CRITICAL();
    g_ota.state = OTA_SERVICE_STATE_ROLLBACK_REQUIRED;
    taskEXIT_CRITICAL();

    if (NULL != ops.request_rollback)
    {
        (void) ops.request_rollback();
    }
    return false;
}

/* 作用: 返回 OTA 状态机当前状态，供上层诊断或对 App 回报。 */
ota_service_state_t ota_service_get_state(void)
{
    ota_service_state_t state;

    taskENTER_CRITICAL();
    state = g_ota.state;
    taskEXIT_CRITICAL();
    return state;
}

static bool     g_ota_test_start_result;
static bool     g_ota_test_mark_pending_result;
static bool     g_ota_test_confirm_result;
static bool     g_ota_test_rollback_result;
static uint32_t g_ota_test_start_count;
static uint32_t g_ota_test_mark_pending_count;
static uint32_t g_ota_test_confirm_count;
static uint32_t g_ota_test_rollback_count;

/* 作用: 重置 OTA 模拟平台的返回值和调用计数。 */
static void ota_test_reset_platform(void)
{
    g_ota_test_start_result = true;
    g_ota_test_mark_pending_result = true;
    g_ota_test_confirm_result = true;
    g_ota_test_rollback_result = true;
    g_ota_test_start_count = 0U;
    g_ota_test_mark_pending_count = 0U;
    g_ota_test_confirm_count = 0U;
    g_ota_test_rollback_count = 0U;
}

/* 作用: 模拟 OTA 下载任务启动，并验证传入清单有效。 */
static bool ota_test_start(ota_manifest_t const * p_manifest)
{
    g_ota_test_start_count++;
    return g_ota_test_start_result && ota_manifest_valid(p_manifest);
}

/* 作用: 模拟 Bootloader 标记待启动镜像。 */
static bool ota_test_mark_pending(ota_manifest_t const * p_manifest)
{
    g_ota_test_mark_pending_count++;
    return g_ota_test_mark_pending_result && ota_manifest_valid(p_manifest);
}

/* 作用: 模拟应用确认当前试运行镜像。 */
static bool ota_test_confirm(void)
{
    g_ota_test_confirm_count++;
    return g_ota_test_confirm_result;
}

/* 作用: 模拟 Bootloader 回滚请求。 */
static bool ota_test_rollback(void)
{
    g_ota_test_rollback_count++;
    return g_ota_test_rollback_result;
}

/* 作用: 返回 OTA 自检使用的可控平台回调集合。 */
static ota_service_platform_ops_t const * ota_test_get_ops(void)
{
    static ota_service_platform_ops_t const ops =
    {
        .start_download = ota_test_start,
        .mark_pending_boot = ota_test_mark_pending,
        .confirm_running_image = ota_test_confirm,
        .request_rollback = ota_test_rollback,
    };

    return &ops;
}

/* 作用: 验证无平台、断网和无效清单不会启动 OTA 或进入试运行状态。 */
static bool ota_test_not_ready_paths(ota_manifest_t const * p_manifest)
{
    ota_service_init(NULL);
    ota_service_notify_trial_boot();
    if ((OTA_SERVICE_STATE_IDLE != ota_service_get_state()) ||
        ota_service_request_update(p_manifest, true))
    {
        return false;
    }

    ota_service_init(ota_test_get_ops());
    ota_service_notify_trial_boot();
    return (OTA_SERVICE_STATE_IDLE == ota_service_get_state()) &&
           !ota_service_request_update(p_manifest, false) &&
           !ota_service_request_update(NULL, true) &&
           (0U == g_ota_test_start_count);
}

/* 作用: 验证下载无法启动、下载失败和 SHA 校验失败均进入失败状态。 */
static bool ota_test_download_failures(ota_manifest_t const * p_manifest)
{
    ota_test_reset_platform();
    g_ota_test_start_result = false;
    ota_service_init(ota_test_get_ops());
    if (ota_service_request_update(p_manifest, true) ||
        (OTA_SERVICE_STATE_FAILED != ota_service_get_state()) ||
        (1U != g_ota_test_start_count))
    {
        return false;
    }

    ota_test_reset_platform();
    ota_service_init(ota_test_get_ops());
    if (!ota_service_request_update(p_manifest, true))
    {
        return false;
    }
    ota_service_notify_download_result(false, true);
    if ((OTA_SERVICE_STATE_FAILED != ota_service_get_state()) ||
        (0U != g_ota_test_mark_pending_count))
    {
        return false;
    }

    ota_test_reset_platform();
    ota_service_init(ota_test_get_ops());
    if (!ota_service_request_update(p_manifest, true))
    {
        return false;
    }
    ota_service_notify_download_result(true, false);
    return (OTA_SERVICE_STATE_FAILED == ota_service_get_state()) &&
           (0U != g_ota_test_start_count) &&
           (0U == g_ota_test_mark_pending_count);
}

/* 作用: 验证待启动标记失败时不会误进入试运行。 */
static bool ota_test_pending_boot_failure(ota_manifest_t const * p_manifest)
{
    ota_test_reset_platform();
    g_ota_test_mark_pending_result = false;
    ota_service_init(ota_test_get_ops());
    if (!ota_service_request_update(p_manifest, true))
    {
        return false;
    }

    ota_service_notify_download_result(true, true);
    ota_service_notify_trial_boot();
    return (OTA_SERVICE_STATE_FAILED == ota_service_get_state()) &&
           (1U == g_ota_test_mark_pending_count) &&
           (0U == g_ota_test_confirm_count);
}

/* 作用: 进入试运行状态，供确认失败和回滚路径复用。 */
static bool ota_test_enter_trial_boot(ota_manifest_t const * p_manifest)
{
    ota_test_reset_platform();
    ota_service_init(ota_test_get_ops());
    if (!ota_service_request_update(p_manifest, true))
    {
        return false;
    }

    ota_service_notify_download_result(true, true);
    if (OTA_SERVICE_STATE_PENDING_BOOT != ota_service_get_state())
    {
        return false;
    }

    ota_service_notify_trial_boot();
    return OTA_SERVICE_STATE_TRIAL_BOOT == ota_service_get_state();
}

/* 作用: 验证应用自检失败或镜像确认失败时都会请求回滚。 */
static bool ota_test_confirm_and_rollback_failures(ota_manifest_t const * p_manifest)
{
    if (!ota_test_enter_trial_boot(p_manifest) ||
        ota_service_confirm_running_image(false) ||
        (OTA_SERVICE_STATE_ROLLBACK_REQUIRED != ota_service_get_state()) ||
        (0U != g_ota_test_confirm_count) ||
        (1U != g_ota_test_rollback_count))
    {
        return false;
    }

    if (!ota_test_enter_trial_boot(p_manifest))
    {
        return false;
    }
    g_ota_test_confirm_result = false;
    if (ota_service_confirm_running_image(true) ||
        (OTA_SERVICE_STATE_ROLLBACK_REQUIRED != ota_service_get_state()) ||
        (1U != g_ota_test_confirm_count) ||
        (1U != g_ota_test_rollback_count))
    {
        return false;
    }

    if (!ota_test_enter_trial_boot(p_manifest))
    {
        return false;
    }
    g_ota_test_rollback_result = false;
    return !ota_service_confirm_running_image(false) &&
           (OTA_SERVICE_STATE_ROLLBACK_REQUIRED == ota_service_get_state()) &&
           (1U == g_ota_test_rollback_count);
}

/* 作用: 验证正常确认路径和全部 OTA 异常状态流转。 */
bool ota_service_self_test(void)
{
    ota_manifest_t manifest = {"0.2.0", "https://example.invalid/fw.bin", {0}, 1024U};

    ota_test_reset_platform();
    if (!ota_test_not_ready_paths(&manifest) ||
        !ota_test_download_failures(&manifest) ||
        !ota_test_pending_boot_failure(&manifest) ||
        !ota_test_confirm_and_rollback_failures(&manifest) ||
        !ota_test_enter_trial_boot(&manifest) ||
        !ota_service_confirm_running_image(true) ||
        (OTA_SERVICE_STATE_CONFIRMED != ota_service_get_state()) ||
        (1U != g_ota_test_confirm_count) ||
        (0U != g_ota_test_rollback_count))
    {
        ota_service_init(NULL);
        return false;
    }

    ota_service_init(NULL);
    return OTA_SERVICE_STATE_IDLE == ota_service_get_state();
}
