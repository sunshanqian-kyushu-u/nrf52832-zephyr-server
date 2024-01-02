#include <zephyr/kernel.h>

/* BT推荐引入的库 */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

/* 控制台库 */
#include <zephyr/sys/printk.h>

#include <dk_buttons_and_leds.h>

/* 用于设置MAC地址 */
#include <zephyr/bluetooth/controller.h>

/* 设备名，会被自动编译进autoconf.h */
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME

/***
 * 定义服务
 * 需要注意的是下面会添加特征，那个也需要一个UUID，但是和这个服务不是同一个东西
 ***/
#define BT_UUID_LED_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define BT_UUID_LED_SERVICE \
	BT_UUID_DECLARE_128(BT_UUID_LED_SERVICE_VAL)

/* 定义特征LED状态 */
#define BT_UUID_LED_STATUS_CHARACTERISTIC_VAL \
	BT_UUID_128_ENCODE(0x12345679, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define BT_UUID_LED_STATUS_CHARACTERISTIC \
	BT_UUID_DECLARE_128(BT_UUID_LED_STATUS_CHARACTERISTIC_VAL)

/* 定义特征LED控制 */
#define BT_UUID_LED_CONTROL_CHARACTERISTIC_VAL \
	BT_UUID_128_ENCODE(0x1234567a, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define BT_UUID_LED_CONTROL_CHARACTERISTIC \
	BT_UUID_DECLARE_128(BT_UUID_LED_CONTROL_CHARACTERISTIC_VAL)

static uint8_t led_status = 0;
static uint8_t led_control = 0;

void on_sent(struct bt_conn *conn, void *user_data) {
	ARG_UNUSED(user_data);
	printk("Notification sent on connection %p\n", (void *)conn);
}

const struct bt_gatt_service_static led_service;

int send_led_notification(struct bt_conn *conn, uint8_t value, uint16_t length) {
	int err = 0;

	struct bt_gatt_notify_params params = {0};
	const struct bt_gatt_attr *attr = &led_service.attrs[2];

	params.attr = attr;
	params.data = &value;
	params.len = length;
	params.func = on_sent;

	err = bt_gatt_notify_cb(conn, &params);

	return err;
}

static struct bt_conn *current_conn;

/* 客户端控制通知开关 */
void led_status_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value) {
	int err;

	printk("Notifications %s", (value == BT_GATT_CCC_NOTIFY) ? "enabled\n" : "disabled\n");
	err = send_led_notification(current_conn, led_status, 1);
	if(err) {
		printk("send notification failed (err %d)\n", err);
		return 0;
	}
}

/* 接收led control */
static ssize_t write_led_control_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, 
		const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
	int err;

	printk("Received data, conn %p, len %d\n", (void *)conn, len);
	uint8_t temp_str[len + 1];
	memcpy(temp_str, buf, len);
	temp_str[len] = 0x00;

	switch (temp_str[0])
	{
	case DK_BTN1_MSK:
		(led_status & DK_BTN1_MSK) >> DK_BTN1 ? dk_set_led_off(DK_LED1) : dk_set_led_on(DK_LED1);
		led_status ^= DK_BTN1_MSK;
		break;
	case DK_BTN2_MSK:
		(led_status & DK_BTN2_MSK) >> DK_BTN2 ? dk_set_led_off(DK_LED2) : dk_set_led_on(DK_LED2);
		led_status ^= DK_BTN2_MSK;
		break;
	case DK_BTN3_MSK:
		(led_status & DK_BTN3_MSK) >> DK_BTN3 ? dk_set_led_off(DK_LED3) : dk_set_led_on(DK_LED3);
		led_status ^= DK_BTN3_MSK;
		break;
	case DK_BTN4_MSK:
		(led_status & DK_BTN4_MSK) >> DK_BTN4 ? dk_set_led_off(DK_LED4) : dk_set_led_on(DK_LED4);
		led_status ^= DK_BTN4_MSK;
		break;
	default:
		break;
	}

	err = send_led_notification(current_conn, led_status, 1);
	if(err) {
		printk("send notification failed (err %d)\n", err);
		return 0;
	}
}

static ssize_t read_led_status_cb(struct bt_conn *conn, 
		const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {

	const char *value = attr->user_data; // 建立一个指向led_status的指针

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(*value));
}

/* 可以理解成将特征绑定到服务上，name可以自己定义 */
BT_GATT_SERVICE_DEFINE(led_service, 
	BT_GATT_PRIMARY_SERVICE(BT_UUID_LED_SERVICE), 
	BT_GATT_CHARACTERISTIC(BT_UUID_LED_STATUS_CHARACTERISTIC, 
			BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, 
			BT_GATT_PERM_READ, 
			read_led_status_cb, 
			NULL, 
			&led_status), 
	BT_GATT_CCC(led_status_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), 
	BT_GATT_CHARACTERISTIC(BT_UUID_LED_CONTROL_CHARACTERISTIC, 
			BT_GATT_CHRC_WRITE_WITHOUT_RESP, 
			BT_GATT_PERM_WRITE, 
			NULL, 
			write_led_control_cb, 
			&led_control), 
);

/***
 * ad是整一个广播包
 * 为了避免直接填写16进制数据的麻烦，提供了宏辅助填写
 * BT_DATA_BYTES的第一个参数是后面数据的类型，如16位长度或者128位长度
 * 第二个参数是具体的服务类型，比如BT_UUID_16_ENCODE(BT_UUID_CTS_VAL)
 * 就是实时时钟服务
 ***/
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LED_SERVICE_VAL),
};

/***
 * sd是扫描回复包
 * 当受到扫描时，回复sd的内容
 ***/
// static const struct bt_data sd[] = {
// 	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LED_SERVICE_VAL),
// };

void conn_connected(struct bt_conn *conn, uint8_t err) {
	if(err) {
		printk("Connect failed (err %d)\n", err);
		return 0;
	}
	current_conn = bt_conn_ref(conn);
	printk("Connected\n");
}

void conn_disconnected(struct bt_conn *conn, uint8_t reason) {
	if(current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
		printk("Disconnected (reason %d)\n", reason);
	}
}

/***
 * 这是连接的回调函数
 * 两个函数分别在连接以及断开的时候触发
 ***/
static struct bt_conn_cb conn_callbacks = {
	.connected = conn_connected,
	.disconnected = conn_disconnected,
};

/* MAC地址 */
uint8_t MACADDRESS[BT_ADDR_SIZE] = {0x52, 0x42, 0x54, 0x00, 0x00, 0x00};

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	int err;

	if (has_changed & button_state) {
		switch (has_changed) {
			case DK_BTN1_MSK:
				(led_status & DK_BTN1_MSK) >> DK_BTN1 ? dk_set_led_off(DK_LED1) : dk_set_led_on(DK_LED1);
				led_status ^= DK_BTN1_MSK;
				break;
			case DK_BTN2_MSK:
				(led_status & DK_BTN2_MSK) >> DK_BTN2 ? dk_set_led_off(DK_LED2) : dk_set_led_on(DK_LED2);
				led_status ^= DK_BTN2_MSK;
				break;
			case DK_BTN3_MSK:
				(led_status & DK_BTN3_MSK) >> DK_BTN3 ? dk_set_led_off(DK_LED3) : dk_set_led_on(DK_LED3);
				led_status ^= DK_BTN3_MSK;
				break;
			case DK_BTN4_MSK:
				(led_status & DK_BTN4_MSK) >> DK_BTN4 ? dk_set_led_off(DK_LED4) : dk_set_led_on(DK_LED4);
				led_status ^= DK_BTN4_MSK;
				break;
			default:
				break;
		}
	}

	err = send_led_notification(current_conn, led_status, 1);
	if(err) {
			printk("send notification failed (err %d)\n", err);
			return 0;
	}
}

int main(void) {
    int err;

    err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return 0;
	}

	dk_set_leds(DK_ALL_LEDS_MSK); // 打开所有LED
	led_status = 0x0f;
	printk("LEDs init succeed\n");

	err = dk_buttons_init(button_handler);
	if (err) {
		printk("Button init failed (err %d)\n", err);
		return 0;
	}

	printk("Button init succeed\n");

	/* 设置MAC地址 */
	bt_ctlr_set_public_addr(MACADDRESS);

	/* 注册一个回调函数 */
	bt_conn_cb_register(&conn_callbacks);

	/* 通用初始化函数 */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth init succeed\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/***
	 * 开始广播
	 * 在传统广播里，广播包里携带的数据只有31个字节
	 * 这31个字节又分为有效部分和无效部分
	 * 有效部分是我们想要发送的数据，由一个个数据单元组成
	 * 每个数据单元又是由长度、类型和数据组成
	 * 长度和类型各占一个字节，数据占长度-1个字节
	 ***/
	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printk("Advertising started\n");

    return 0;
}
