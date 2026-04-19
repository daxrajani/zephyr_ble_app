#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>
#include <inttypes.h>
#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ------------------------------------------------------------------------- */
/* CUSTOM UUID DEFINITIONS                                                   */
/* ------------------------------------------------------------------------- */
#define UUID_CUSTOM_SERVICE \
    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x773c49e4, 0x6ad5, 0x4035, 0xa3de, 0x495293c372c6))

#define UUID_CUSTOM_TEMP \
    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x773c49e5, 0x6ad5, 0x4035, 0xa3de, 0x495293c372c6))

#define UUID_CUSTOM_HUMIDITY \
    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x773c49e6, 0x6ad5, 0x4035, 0xa3de, 0x495293c372c6))

#define UUID_CUSTOM_SAMPLE_RATE \
    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x773c49e7, 0x6ad5, 0x4035, 0xa3de, 0x495293c372c6))

/* ------------------------------------------------------------------------- */
/* SENSOR DATA & STATE                                                       */
/* ------------------------------------------------------------------------- */
static int16_t sensor_temp = 2400;     
static uint16_t sensor_humidity = 4550; 
static uint32_t sample_rate_ms = 1000;  

static struct bt_conn *current_conn = NULL;
static struct bt_le_ext_adv *ext_adv_set; 
static struct k_work adv_work;

/* PER-CHARACTERISTIC NOTIFICATION FLAGS */
static bool notify_temp_enabled = false;
static bool notify_humidity_enabled = false;

/* ------------------------------------------------------------------------- */
/* SCALABLE GATT READ ARCHITECTURE & CALLBACKS                               */
/* ------------------------------------------------------------------------- */
struct sensor_value {
    void *data;
    size_t size;
};

static struct sensor_value temp_value = { .data = &sensor_temp, .size = sizeof(sensor_temp) };
static struct sensor_value humidity_value = { .data = &sensor_humidity, .size = sizeof(sensor_humidity) };
static struct sensor_value sample_rate_value = { .data = &sample_rate_ms, .size = sizeof(sample_rate_ms) };

static ssize_t read_sensor_data(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    struct sensor_value *sv = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, sv->data, sv->size);
}

static ssize_t write_sample_rate(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset + len > sizeof(sample_rate_ms)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    memcpy(&sample_rate_ms, buf, len);
    LOG_INF("Phone updated Sample Rate to: %" PRIu32 " ms", sample_rate_ms);
    settings_save_one("sensor/rate", &sample_rate_ms, sizeof(sample_rate_ms));
    return len;
}

/* DEDICATED CCCD CALLBACKS */
static void temp_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_temp_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Temperature Notifications %s", notify_temp_enabled ? "enabled" : "disabled");
}

static void humidity_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_humidity_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Humidity Notifications %s", notify_humidity_enabled ? "enabled" : "disabled");
}

/* ------------------------------------------------------------------------- */
/* THE GATT SERVICE TABLE                                                    */
/* ------------------------------------------------------------------------- */
BT_GATT_SERVICE_DEFINE(my_sensor_svc,
    BT_GATT_PRIMARY_SERVICE(UUID_CUSTOM_SERVICE),
    
    BT_GATT_CHARACTERISTIC(UUID_CUSTOM_TEMP,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, 
                           read_sensor_data, NULL, &temp_value),
    BT_GATT_CCC(temp_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                           
    BT_GATT_CHARACTERISTIC(UUID_CUSTOM_HUMIDITY,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, 
                           read_sensor_data, NULL, &humidity_value),
    BT_GATT_CCC(humidity_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                           
    BT_GATT_CHARACTERISTIC(UUID_CUSTOM_SAMPLE_RATE,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, 
                           read_sensor_data, write_sample_rate, &sample_rate_value),
);

/* ------------------------------------------------------------------------- */
/* RTOS BACKGROUND SENSOR THREAD                                             */
/* ------------------------------------------------------------------------- */
static void sensor_thread_func(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int8_t temp_dir = 1;
    int8_t hum_dir = 1;

    while (1) {
        k_sleep(K_MSEC(sample_rate_ms));

        sensor_temp += (temp_dir * 5);       
        if (sensor_temp >= 3000 || sensor_temp <= 2000) temp_dir *= -1;

        sensor_humidity += (hum_dir * 15);  
        if (sensor_humidity >= 7000 || sensor_humidity <= 3000) hum_dir *= -1;

        /* INDEPENDENT NOTIFICATION CHECKS */
        if (current_conn) {
            if (notify_temp_enabled) {
                bt_gatt_notify(current_conn, &my_sensor_svc.attrs[2], &sensor_temp, sizeof(sensor_temp));
            }
            if (notify_humidity_enabled) {
                bt_gatt_notify(current_conn, &my_sensor_svc.attrs[5], &sensor_humidity, sizeof(sensor_humidity));
            }
            
            if (notify_temp_enabled || notify_humidity_enabled) {
                LOG_INF("Pushed -> Temp: %d, Hum: %d", sensor_temp, sensor_humidity);
            }
        }
    }
}

K_THREAD_DEFINE(sensor_thread, 1024, sensor_thread_func, NULL, NULL, NULL, 7, 0, 0);

/* ------------------------------------------------------------------------- */
/* WORK QUEUE & CONNECTION HANDLERS                                          */
/* ------------------------------------------------------------------------- */
static void adv_work_handler(struct k_work *work)
{
    int err;
    if (ext_adv_set) {
        err = bt_le_ext_adv_start(ext_adv_set, BT_LE_EXT_ADV_START_DEFAULT);
        if (err) {
            LOG_ERR("Failed to restart advertising (err %d)", err);
        } else {
            LOG_INF("Advertising restarted successfully");
        }
    }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }
    LOG_INF("Phone Connected!");
    current_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Phone Disconnected (reason 0x%02x)", reason);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    
    /* RESET BOTH FLAGS UPON DISCONNECT */
    notify_temp_enabled = false;
    notify_humidity_enabled = false;
    
    k_work_submit(&adv_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* ------------------------------------------------------------------------- */
/* MAIN ROUTINE                                                              */
/* ------------------------------------------------------------------------- */
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static int settings_set_cb(const char *name, size_t len,
                           settings_read_cb read_cb, void *cb_arg)
{
    ARG_UNUSED(len);

    if (strcmp(name, "rate") == 0) {
        read_cb(cb_arg, &sample_rate_ms, sizeof(sample_rate_ms));
        LOG_INF("Loaded sample rate: %" PRIu32 " ms", sample_rate_ms);
    }
    return 0;
}

static struct settings_handler my_settings = {
    .name = "sensor",
    .h_set = settings_set_cb,
};

int main(void)
{
    int err;

    LOG_INF("Starting Dax_BLE Phase 4 (Settings Persistence) Application...");

    k_work_init(&adv_work, adv_work_handler);

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    err = settings_subsys_init();
    if (err) {
        LOG_ERR("Settings init failed (err %d)", err);
        return 0;
    }

    err = settings_register(&my_settings);
    if (err) {
        LOG_ERR("Settings register failed (err %d)", err);
        return 0;
    }

    err = settings_load();
    if (err) {
        LOG_ERR("Settings load failed (err %d)", err);
        return 0;
    }

    err = bt_le_ext_adv_create(BT_LE_EXT_ADV_CONN, NULL, &ext_adv_set);
    if (err) {
        LOG_ERR("Failed to create advertising set (err %d)", err);
        return 0;
    }

    err = bt_le_ext_adv_set_data(ext_adv_set, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Failed to set advertising data (err %d)", err);
        return 0;
    }

    err = bt_le_ext_adv_start(ext_adv_set, BT_LE_EXT_ADV_START_DEFAULT);
    if (err) {
        LOG_ERR("Failed to start extended advertising (err %d)", err);
        return 0;
    }

    LOG_INF("Extended Advertising started. Waiting for connection...");

    while (1) {
        k_sleep(K_FOREVER);
    }
}