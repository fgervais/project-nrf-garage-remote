#include <zephyr/kernel.h>
#include <zephyr/net/coap_service.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(door_coap_service);

#include <stdio.h>

struct request {
	struct sockaddr addr;
	uint16_t id;
	k_timepoint_t timeout;
};

static struct request m_requests[CONFIG_COAP_SERVICE_PENDING_MESSAGES];

static int get_free_request(struct request *request)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(m_requests), i++) {
		if (sys_timepoint_expired(m_requests.timeout)) {
			request = &m_requests[i];
			return 0;
		}
	}

	return -ENOMEM;
}

static bool is_request_answered(struct sockaddr *addr, uint16_t id)
{
	int i;
	const struct sockaddr_in6 *a6;
	const struct sockaddr_in6 *b6;

	for (i = 0; i < ARRAY_SIZE(m_requests), i++) {
		a6 = net_sin6(addr);
		a6 = net_sin6(&m_requests.addr);

		if (a6->sin6_port != b6->sin6_port) {
			continue;
		}

		if (!net_ipv6_addr_cmp(&a6->sin6_addr, &b6->sin6_addr)) {
			continue;
		}

		if (m_requests.id != id) {
			continue;
		}

		if (sys_timepoint_expired(m_requests.timeout)) {
			continue;
		}

		return true;
	}

	return false;
}

static int set_request_answered(struct sockaddr *addr, uint16_t id)
{
	int i;
	const struct sockaddr_in6 *a6;
	const struct sockaddr_in6 *b6;

	for (i = 0; i < ARRAY_SIZE(m_requests), i++) {
		if (sys_timepoint_expired(m_requests.timeout)) {
			continue;
		}

		net_ipaddr_copy(&m_requests.addr, addr);
		m_requests.id = id;
		m_requests.timeout = sys_timepoint_calc(K_HOURS(COAP_INIT_ACK_TIMEOUT_MS * 3));

		return 0;
	}

	return -ENOMEM;
}

static int door_get(struct coap_resource *resource, struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	uint8_t data[CONFIG_COAP_SERVER_MESSAGE_SIZE];
	struct coap_packet response;
	uint8_t payload[40];
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t token_length;
	int r;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	token_length = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	r = coap_packet_init(&response, data, sizeof(data), COAP_VERSION_1, type, token_length,
			     token, COAP_RESPONSE_CODE_CONTENT, id);
	if (r < 0) {
		return r;
	}

	r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
				   COAP_CONTENT_FORMAT_TEXT_PLAIN);
	if (r < 0) {
		return r;
	}

	r = coap_packet_append_payload_marker(&response);
	if (r < 0) {
		return r;
	}

	r = snprintf(payload, sizeof(payload), "Type: %u\nCode: %u\nMID: %u\n", type, code, id);
	if (r < 0) {
		return r;
	}

	r = coap_packet_append_payload(&response, (uint8_t *)payload, strlen(payload));
	if (r < 0) {
		return r;
	}

	r = coap_resource_send(resource, &response, addr, addr_len, NULL);

	return r;
}

static int door_post(struct coap_resource *resource, struct coap_packet *request,
		     struct sockaddr *addr, socklen_t addr_len)
{
	uint8_t data[CONFIG_COAP_SERVER_MESSAGE_SIZE];
	struct coap_packet response;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	const uint8_t *payload;
	uint16_t payload_len;
	uint8_t code;
	uint8_t type;
	uint8_t token_length;
	uint16_t id;
	int ret;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	token_length = coap_header_get_token(request, token);

	LOG_INF("📬 POST (door)");
	LOG_INF("└── type: %u code %u id %u", type, code, id);

	payload = coap_packet_get_payload(request, &payload_len);
	if (payload) {
		LOG_HEXDUMP_INF(payload, payload_len, "POST Payload");
	}

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	ret = coap_packet_init(&response, data, sizeof(data), COAP_VERSION_1, type, token_length,
			       token, COAP_RESPONSE_CODE_CHANGED, id);
	if (ret < 0) {
		return ret;
	}

	ret = coap_resource_send(resource, &response, addr, addr_len, NULL);

	return ret;
}

int door_init(void)
{
	return 0;
}

static const char *const door_path[] = {"door", NULL};
COAP_RESOURCE_DEFINE(door, coap_server,
		     {
			     .get = door_get,
			     .post = door_post,
			     .path = door_path,
		     });
