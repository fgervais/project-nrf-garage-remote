#include <app_event_manager.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap_service.h>
#include <zephyr/net/net_if.h>
#include <zephyr/pm/device.h>
#include <zephyr/debug/thread_analyzer.h>

#define MODULE main
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <app_version.h>
#include <mymodule/base/openthread.h>
#include <mymodule/base/reset.h>
#include <mymodule/base/watchdog.h>

#include "door.h"

#define BUTTON_PRESS_EVENT BIT(0)

#define ALL_NODES_LOCAL_COAP_MCAST                                                                 \
	{                                                                                          \
		{                                                                                  \
			{                                                                          \
				0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfd            \
			}                                                                          \
		}                                                                                  \
	}

static K_EVENT_DEFINE(button_events);

static const uint16_t coap_port = 5683;
COAP_SERVICE_DEFINE(coap_server, NULL, &coap_port, 0);

int join_coap_multicast_group(void)
{
	struct in6_addr mcast_addr6 = ALL_NODES_LOCAL_COAP_MCAST;
	struct net_if_mcast_addr *mcast;
	struct net_if *iface;

	LOG_INF("joining the COAP multicast group");

	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("Could not get the default interface");
		return -ENOENT;
	}

	mcast = net_if_ipv6_maddr_add(iface, &mcast_addr6);
	if (!mcast) {
		LOG_ERR("Could not add multicast address to interface");
		return -ENOENT;
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

	reset_cause = show_and_clear_reset_cause();

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
	openthread_wait(OT_ROLE_SET | OT_MESH_LOCAL_ADDR_SET);

	ret = join_coap_multicast_group();
	if (ret < 0) {
		LOG_ERR("Could not join the COAP multicast group");
		return ret;
	}

	LOG_INF("starting the COAP service");
	ret = door_init();
	if (ret < 0) {
		LOG_ERR("Could not init door module");
		return ret;
	}
	ret = coap_service_start(&coap_server);
	if (ret < 0) {
		LOG_ERR("Could not start COAP service");
		return ret;
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
		events = k_event_wait(&button_events, (BUTTON_PRESS_EVENT), true,
				      K_SECONDS(CONFIG_APP_MAIN_LOOP_PERIOD_SEC));

		LOG_INF("⏰ events: %08x", events);

		if (events & BUTTON_PRESS_EVENT) {
			LOG_INF("handling button press event");
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
