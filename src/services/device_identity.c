#include "device_identity.h"

#include <string.h>

static char g_device_id[DEVICE_IDENTITY_MAX_LENGTH + 1U];
static bool g_device_id_ready;

static bool device_identity_is_valid(char const * p_device_id)
{
    size_t length = 0U;

    if (NULL == p_device_id)
    {
        return false;
    }

    while ('\0' != p_device_id[length])
    {
        char const character = p_device_id[length];
        bool const valid = ((character >= 'A') && (character <= 'Z')) ||
                           ((character >= 'a') && (character <= 'z')) ||
                           ((character >= '0') && (character <= '9')) ||
                           ('_' == character) || ('-' == character);

        if (!valid || (length >= DEVICE_IDENTITY_MAX_LENGTH))
        {
            return false;
        }

        ++length;
    }

    return (0U != length);
}

void device_identity_init(device_identity_read_callback_t read_callback)
{
    (void) memset(g_device_id, 0, sizeof(g_device_id));
    g_device_id_ready = false;

    if (NULL != read_callback)
    {
        bool const read_success = read_callback(g_device_id, sizeof(g_device_id));

        /* 即使底层回调写满缓冲区，也强制保留字符串结尾。 */
        g_device_id[DEVICE_IDENTITY_MAX_LENGTH] = '\0';
        if (read_success && device_identity_is_valid(g_device_id))
        {
            g_device_id_ready = true;
            return;
        }
    }

    (void) memset(g_device_id, 0, sizeof(g_device_id));
}

bool device_identity_get(char * p_device_id, size_t device_id_size)
{
    size_t const required_size = strlen(g_device_id) + 1U;

    if (!g_device_id_ready || (NULL == p_device_id) || (device_id_size < required_size))
    {
        return false;
    }

    (void) memcpy(p_device_id, g_device_id, required_size);
    return true;
}

static bool device_identity_test_read(char * p_device_id, size_t device_id_size)
{
    static char const test_id[] = "HYGRO-000001";

    if (device_id_size < sizeof(test_id))
    {
        return false;
    }

    (void) memcpy(p_device_id, test_id, sizeof(test_id));
    return true;
}

bool device_identity_self_test(void)
{
    char device_id[DEVICE_IDENTITY_MAX_LENGTH + 1U];

    device_identity_init(device_identity_test_read);
    if (!device_identity_get(device_id, sizeof(device_id)) ||
        (0 != strcmp(device_id, "HYGRO-000001")))
    {
        return false;
    }

    device_identity_init(NULL);
    return !device_identity_get(device_id, sizeof(device_id));
}
