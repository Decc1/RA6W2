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

static bool ota_manifest_valid(ota_manifest_t const * p_manifest)
{
    return (NULL != p_manifest) && ('\0' != p_manifest->version[0]) &&
           ('\0' != p_manifest->url[0]) && (0U != p_manifest->image_size_bytes);
}

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

void ota_service_notify_trial_boot(void)
{
    /*
     * 重启后 RAM 状态已丢失；只有 Bootloader 的持久化升级标志确认
     * “本次为新镜像试运行”时，平台适配层才允许调用本函数。
     */
    g_ota.state = OTA_SERVICE_STATE_TRIAL_BOOT;
}

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

ota_service_state_t ota_service_get_state(void)
{
    return g_ota.state;
}

static bool ota_test_start(ota_manifest_t const * p_manifest)
{
    return ota_manifest_valid(p_manifest);
}

static bool ota_test_manifest(ota_manifest_t const * p_manifest)
{
    (void) p_manifest;
    return true;
}

static bool ota_test_no_argument(void)
{
    return true;
}

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
