#include <zephyr/net/coap_service.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(door_coap_service);

#include <stdio.h>


static int door_get(struct coap_resource *resource,
			 struct coap_packet *request,
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

	r = coap_packet_init(&response, data, sizeof(data),
			     COAP_VERSION_1, type, token_length, token,
			     COAP_RESPONSE_CODE_CONTENT, id);
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

	r = snprintf(payload, sizeof(payload),
		     "Type: %u\nCode: %u\nMID: %u\n", type, code, id);
	if (r < 0) {
		return r;
	}

	r = coap_packet_append_payload(&response, (uint8_t *)payload,
				       strlen(payload));
	if (r < 0) {
		return r;
	}

	r = coap_resource_send(resource, &response, addr, addr_len, NULL);

	return r;
}

static int door_post(struct coap_resource *resource,
		     struct coap_packet *request,
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
	int r;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	token_length = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");

	payload = coap_packet_get_payload(request, &payload_len);
	if (payload) {
		LOG_HEXDUMP_INF(payload, payload_len, "POST Payload");
	}

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	r = coap_packet_init(&response, data, sizeof(data),
			     COAP_VERSION_1, type, token_length, token,
			     COAP_RESPONSE_CODE_CHANGED, id);
	if (r < 0) {
		return r;
	}

	r = coap_resource_send(resource, &response, addr, addr_len, NULL);

	return r;
}

static const char * const door_path[] = { "door", NULL };
COAP_RESOURCE_DEFINE(door, coap_server,
{
	.get = door_get,
	.post = door_post,
	.path = door_path,
});
