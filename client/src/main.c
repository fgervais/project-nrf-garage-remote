#include <app_event_manager.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/net/socket.h>
#include <zephyr/pm/device.h>
#include <zephyr/debug/thread_analyzer.h>

#define MODULE main
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <errno.h>

#include <app_version.h>
#include <mymodule/base/openthread.h>
#include <mymodule/base/reset.h>
#include <mymodule/base/watchdog.h>


#define BUTTON_PRESS_EVENT		BIT(0)

#define ALL_NODES_LOCAL_COAP_MCAST \
        { { { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfd } } }
#define ALL_NODES_MCAST \
        { { { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01 } } }
#define ALL_ROUTERS_MCAST \
        { { { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02 } } }
#define DIRECT_IP6_ADDRESS \
        { { { 0xfd, 0xb5, 0x03, 0x41, 0xc7, 0x36, 0x7c, 0x8d, 0xb5, 0x05, 0xd4, 0x61, 0xec, 0xbd, 0x97, 0x89 } } }
#define DIRECT_LL_IP6_ADDRESS \
        { { { 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0xdc, 0x8c, 0xbc, 0xd0, 0x9a, 0xb5, 0x63, 0xb4 } } }
#define COAP_PORT	5683
#define COAP_PATH	"door"


static K_EVENT_DEFINE(button_events);

static struct coap_client coap_client;


static void on_coap_response(int16_t result_code, size_t offset,
			     const uint8_t *payload, size_t len,
			     bool last_block, void *user_data)
{
	// int *sockfd = (int *)user_data;

	LOG_INF("CoAP response, result_code=%d, offset=%u, len=%u", result_code, offset, len);

	if (result_code == COAP_RESPONSE_CODE_CHANGED) {
		LOG_INF("🎉 CoAP succeeded");
	}
	else {
		LOG_ERR("Error during CoAP download, result_code=%d", result_code);
	}

	openthread_request_normal_latency("coap response");

	// zsock_close(*sockfd);
}

static int toggle_door_state(struct coap_client *client, int sockfd, struct sockaddr *sa)
{
	int ret;
	// int sockfd;
	struct coap_client_request request = {
		.method = COAP_METHOD_POST,
		.confirmable = true,
		.path = COAP_PATH,
		.payload = NULL,
		.len = 0,
		.cb = on_coap_response,
		.options = NULL,
		.num_options = 0,
		// .user_data = &sockfd,
		.user_data = NULL,
	};

	// sockfd = zsock_socket(sa->sa_family, SOCK_DGRAM, 0);
	// if (sockfd < 0) {
	// 	LOG_ERR("Failed to create socket, err %d", errno);
	// 	return -errno;
	// }

	LOG_INF("Starting CoAP request");

	openthread_request_low_latency("coap request");

	ret = coap_client_req(client, sockfd, sa, &request, NULL);
	if (ret) {
		LOG_ERR("Failed to send CoAP request, err %d", ret);
		openthread_request_normal_latency("coap request error");
		return ret;
	}

	return 0;
}

int main(void)
{
	const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));
#if defined(CONFIG_APP_SUSPEND_CONSOLE)
	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#endif
	int ret;
	uint32_t reset_cause;
	int main_wdt_chan_id = -1;
	uint32_t events;
	int sockfd;

	struct sockaddr_in6 sockaddr6 = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(COAP_PORT),
		// .sin6_addr = ALL_NODES_LOCAL_COAP_MCAST,
		.sin6_addr = DIRECT_IP6_ADDRESS,
		// .sin6_addr = ALL_NODES_MCAST,
		// .sin6_addr = ALL_ROUTERS_MCAST,
		// .sin6_addr = DIRECT_LL_IP6_ADDRESS,
	};


	ret = watchdog_new_channel(wdt, &main_wdt_chan_id);
	if (ret < 0) {
		LOG_ERR("Could allocate main watchdog channel");
		return ret;
	}

	ret = watchdog_start(wdt);
	if (ret < 0) {
		LOG_ERR("Could allocate start watchdog");
		return ret;
	}

	LOG_INF("\n\n🚀 MAIN START (%s) 🚀\n", APP_VERSION_FULL);

	reset_cause = show_reset_cause();
	clear_reset_cause();
	
	if (app_event_manager_init()) {
		LOG_ERR("Event manager not initialized");
	} else {
		module_set_state(MODULE_STATE_READY);
	}

	ret = openthread_my_start();
	if (ret < 0) {
		LOG_ERR("Could not start openthread");
		return ret;
	}

	LOG_INF("💤 waiting for openthread to be ready");
	openthread_wait(OT_ROLE_SET | 
			OT_MESH_LOCAL_ADDR_SET | 
			OT_HAS_NEIGHBORS);

	ret = coap_client_init(&coap_client, NULL);
	if (ret) {
		LOG_ERR("Failed to init coap client, err %d", ret);
		return ret;
	}

	sockfd = zsock_socket(sockaddr6.sin6_family, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		LOG_ERR("Failed to create socket, err %d", errno);
		return -errno;
	}

	LOG_INF("🆗 initialized");

#if defined(CONFIG_APP_SUSPEND_CONSOLE)
	ret = pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
	if (ret < 0) {
		LOG_ERR("Could not suspend the console");
		return ret;
	}
#endif

	thread_analyzer_print();

	LOG_INF("┌──────────────────────────────────────────────────────────┐");
	LOG_INF("│ Entering main loop                                       │");
	LOG_INF("└──────────────────────────────────────────────────────────┘");

	while (1) {
		LOG_INF("💤 waiting for events");
		events = k_event_wait(&button_events,
				(BUTTON_PRESS_EVENT),
				true,
				K_SECONDS(CONFIG_APP_MAIN_LOOP_PERIOD_SEC));

		LOG_INF("⏰ events: %08x", events);

		if (events & BUTTON_PRESS_EVENT) {
			LOG_INF("handling button press event");
			ret = toggle_door_state(&coap_client,
						sockfd,
						(struct sockaddr *)&sockaddr6);
			if (ret < 0) {
				LOG_ERR("Could not toggle door state");
			}
		}

		LOG_INF("🦴 feed watchdog");
		wdt_feed(wdt, main_wdt_chan_id);
	}

	return 0;
}

static bool event_handler(const struct app_event_header *eh)
{
	const struct button_event *evt;

	if (is_button_event(eh)) {
		evt = cast_button_event(eh);

		if (evt->pressed) {
			LOG_INF("🛎️  Button pressed");
			k_event_post(&button_events, BUTTON_PRESS_EVENT);
		}
	}

	return true;
}

APP_EVENT_LISTENER(MODULE, event_handler);
APP_EVENT_SUBSCRIBE(MODULE, button_event);
