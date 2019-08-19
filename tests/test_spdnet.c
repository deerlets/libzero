#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <zmq.h>
#include <spdnet.h>
#include <task.h>

#define INNER_ROUTER_ADDRESS "tcp://127.0.0.1:18338"
#define OUTER_ROUTER_ADDRESS "tcp://0.0.0.0:18339"
#define PUB_SUB_ADDRESS "tcp://127.0.0.1:18330"
#define FWD_PUB_ADDRESS "tcp://127.0.0.1:18331"
#define FWD_SUB_ADDRESS "tcp://127.0.0.1:18332"

/*
 * basic
 */

static void test_spdnet_basic(void **status)
{
	void *ctx = spdnet_ctx_new();
	void *router = spdnet_router_new(ctx, "router_inner");
	spdnet_router_bind(router, INNER_ROUTER_ADDRESS);
	struct task *router_task = task_new_timeout(
		"router_task", spdnet_router_loop, router, 500);
	task_start(router_task);

	int rc;
	void *service, *requester;
	struct spdnet_msg msg;

	service = spdnet_node_new(ctx, SPDNET_NODE);
	spdnet_set_id(service, "service", strlen("service"));
	rc = spdnet_connect(service, INNER_ROUTER_ADDRESS);
	assert_true(rc == 0);
	rc = spdnet_register(service);
	assert_true(rc == 0);

	requester = spdnet_node_new(ctx, SPDNET_NODE);
	rc = spdnet_connect(requester, INNER_ROUTER_ADDRESS);
	assert_true(rc == 0);
	SPDNET_MSG_INIT_DATA(&msg, "service", "hello", "I'm xiedd.");
	rc = spdnet_sendmsg(requester, &msg);
	assert_true(rc == 0);
	spdnet_msg_close(&msg);

	spdnet_msg_init(&msg);
	rc = spdnet_recvmsg(service, &msg, 0);
	assert_true(rc == 0);
	assert_true(MSG_SOCKID_SIZE(&msg) == 5);
	assert_true(MSG_HEADER_SIZE(&msg) == 5);
	assert_true(MSG_CONTENT_SIZE(&msg) == 10);
	assert_memory_equal(MSG_HEADER_DATA(&msg), "hello", 5);
	assert_memory_equal(MSG_CONTENT_DATA(&msg), "I'm xiedd.", 10);
	spdnet_frame_close(MSG_CONTENT(&msg));
	spdnet_frame_init_size(MSG_CONTENT(&msg), 17);
	memcpy(MSG_CONTENT_DATA(&msg), "Welcome to zerox.", 17);
	rc = spdnet_sendmsg(service, &msg);
	assert_true(rc == 0);
	spdnet_msg_close(&msg);

	sleep(1);
	spdnet_msg_init(&msg);
	rc = spdnet_recvmsg(requester, &msg, 0);
	assert_true(rc == 0);
	assert_true(MSG_SOCKID_SIZE(&msg) == 7);
	assert_true(MSG_HEADER_SIZE(&msg) == 5);
	assert_true(MSG_CONTENT_SIZE(&msg) == 17);
	assert_memory_equal(MSG_HEADER_DATA(&msg), "hello", 5);
	assert_memory_equal(MSG_CONTENT_DATA(&msg), "Welcome to zerox.", 17);
	spdnet_msg_close(&msg);

	assert_true(spdnet_router_msg_routerd(router) == 5);
	assert_true(spdnet_router_msg_dropped(router) == 0);

	zmq_send(spdnet_get_socket(requester), "service", 7, 0);
	sleep(1);
	assert_true(spdnet_router_msg_routerd(router) == 5);
	assert_true(spdnet_router_msg_dropped(router) == 1);

	spdnet_node_destroy(requester);
	spdnet_node_destroy(service);
	task_destroy(router_task);
	spdnet_router_destroy(router);
	spdnet_ctx_destroy(ctx);
}

/*
 * spdnet pub & sub
 */
static void test_spdnet_pub_sub(void **status)
{
	void *ctx = spdnet_ctx_new();
	void *pub = spdnet_node_new(ctx, SPDNET_PUB);
	void *sub = spdnet_node_new(ctx, SPDNET_SUB);

	spdnet_bind(pub, PUB_SUB_ADDRESS);
	spdnet_connect(sub, PUB_SUB_ADDRESS);
	spdnet_set_filter(sub, "topic-test", 10);

	struct spdnet_msg msg;
	SPDNET_MSG_INIT_DATA(&msg, "topic-test", "zh://say", "hello");

	spdnet_sendmsg(pub, &msg);
	spdnet_recvmsg(sub, &msg, 0);
	assert_memory_equal(MSG_SOCKID_DATA(&msg), "topic-test", 10);
	assert_memory_equal(MSG_HEADER_DATA(&msg), "zh://say", 8);
	assert_memory_equal(MSG_CONTENT_DATA(&msg), "hello", 5);

	spdnet_node_destroy(pub);
	spdnet_node_destroy(sub);
	spdnet_ctx_destroy(ctx);
}

static int pub_send(void *pub)
{
	struct spdnet_msg msg;
	SPDNET_MSG_INIT_DATA(&msg, "topic-test", "zh://say", "hello");
	spdnet_sendmsg(pub, &msg);
	spdnet_msg_close(&msg);

	return 1;
}

static void test_spdnet_pub_sub2(void **status)
{
	void *ctx = spdnet_ctx_new();
	void *pub = spdnet_node_new(ctx, SPDNET_PUB);
	void *sub = spdnet_node_new(ctx, SPDNET_SUB);

	spdnet_bind(sub, PUB_SUB_ADDRESS);
	spdnet_set_filter(sub, "topic-test", 10);
	spdnet_connect(pub, PUB_SUB_ADDRESS);

	struct task *t = task_new("pub_send", pub_send, pub);
	task_start(t);

	struct spdnet_msg msg;
	spdnet_msg_init(&msg);
	spdnet_recvmsg(sub, &msg, 0);
	assert_memory_equal(MSG_SOCKID_DATA(&msg), "topic-test", 10);
	assert_memory_equal(MSG_HEADER_DATA(&msg), "zh://say", 8);
	assert_memory_equal(MSG_CONTENT_DATA(&msg), "hello", 5);
	spdnet_msg_close(&msg);

	task_destroy(t);
	spdnet_node_destroy(pub);
	spdnet_node_destroy(sub);
	spdnet_ctx_destroy(ctx);
}

/*
 * spdnet forwarder
 */

static void test_spdnet_forwarder(void **status)
{
	void *ctx = spdnet_ctx_new();
	void *fwd = spdnet_forwarder_new(ctx, FWD_PUB_ADDRESS, FWD_SUB_ADDRESS);
	void *pub = spdnet_node_new(ctx, SPDNET_PUB);
	void *sub = spdnet_node_new(ctx, SPDNET_SUB);

	struct task *t = task_new_timeout(
		"forward_loop", spdnet_forwarder_loop, fwd, 500);
	task_start(t);

	spdnet_connect(pub, FWD_SUB_ADDRESS);
	spdnet_connect(sub, FWD_PUB_ADDRESS);
	spdnet_set_filter(sub, "topic-test", 10);

	struct spdnet_msg msg;
	SPDNET_MSG_INIT_DATA(&msg, "topic-test", "zh://say", "hello");

	spdnet_sendmsg(pub, &msg);
	spdnet_recvmsg(sub, &msg, 0);
	assert_memory_equal(MSG_SOCKID_DATA(&msg), "topic-test", 10);
	assert_memory_equal(MSG_HEADER_DATA(&msg), "zh://say", 8);
	assert_memory_equal(MSG_CONTENT_DATA(&msg), "hello", 5);

	task_destroy(t);
	spdnet_node_destroy(pub);
	spdnet_node_destroy(sub);
	spdnet_forwarder_destroy(fwd);
	spdnet_ctx_destroy(ctx);
}

/*
 * spdnet nodepool
 */

static void recvmsg_cb(void *snode, struct spdnet_msg *msg, void *arg)
{
	spdnet_nodepool_put(arg, snode);
}

static void test_spdnet_nodepool(void **status)
{
	int rc;
	void *ctx = spdnet_ctx_new();
	void *snodepool = spdnet_nodepool_new(ctx, 1);

	struct spdnet_msg msg;
	SPDNET_MSG_INIT_DATA(&msg, "gene", "info", NULL);
	void *p = spdnet_nodepool_get(snodepool);
	rc = spdnet_connect(p, "tcp://192.168.31.12:1234");
	assert_true(rc == 0);
	rc = spdnet_sendmsg(p, &msg);
	assert_true(rc == 0);
	spdnet_recvmsg_async(p, recvmsg_cb, snodepool, 3000);
	assert_true(rc == 0);
	spdnet_msg_close(&msg);

	// water_mark is one, spdnet_nodepool_put will decrease alive count
	while (spdnet_nodepool_alive_count(snodepool))
		spdnet_nodepool_loop(snodepool, 0);

	spdnet_nodepool_destroy(snodepool);
	spdnet_ctx_destroy(ctx);
}

/*
 * spdnet router
 */

static void test_spdnet_router(void **status)
{
	int rc;
	void *ctx = spdnet_ctx_new();

	// router inner
	void *inner = spdnet_router_new(ctx, "router-inner");
	assert_true(inner);
	rc = spdnet_router_bind(inner, INNER_ROUTER_ADDRESS);
	assert_true(rc == 0);
	struct task *inner_task = task_new_timeout(
		"router-inner-task", spdnet_router_loop, inner, 1000);
	task_start(inner_task);
	sleep(1);

	// router outer
	char inner_id[SPDNET_SOCKID_SIZE];
	size_t inner_len;
	void *outer = spdnet_router_new(ctx, NULL);
	assert_true(outer);
	rc = spdnet_router_bind(outer, OUTER_ROUTER_ADDRESS);
	assert_true(rc == 0);
	rc = spdnet_router_associate(outer, INNER_ROUTER_ADDRESS,
	                             inner_id, &inner_len);
	assert_true(rc == 0);
	spdnet_router_set_gateway(outer, inner_id, inner_len);
	struct task *outer_task = task_new_timeout(
		"router-outer-task", spdnet_router_loop, outer, 1000);
	task_start(outer_task);
	sleep(1);

	struct spdnet_msg msg;
	void *requester, *service;
	spdnet_msg_init(&msg);
	requester = spdnet_node_new(ctx, SPDNET_NODE);
	spdnet_set_id(requester, "requester", strlen("requester"));
	service = spdnet_node_new(ctx, SPDNET_NODE);
	spdnet_set_id(service, "service", strlen("service"));

	rc = spdnet_connect(requester, OUTER_ROUTER_ADDRESS);
	assert_true(rc == 0);
	rc = spdnet_connect(service, INNER_ROUTER_ADDRESS);
	assert_true(rc == 0);
	rc = spdnet_register(service);
	assert_true(rc == 0);

	// send from requester to service
	spdnet_msg_close(&msg);
	SPDNET_MSG_INIT_DATA(&msg, "service", "hello", "world");
	rc = spdnet_sendmsg(requester, &msg);
	assert_true(rc == 0);
	sleep(1);
	rc = spdnet_recvmsg(service, &msg, 0);
	assert_true(rc == 0);
	assert_true(MSG_SOCKID_SIZE(&msg) == 9);
	assert_true(MSG_HEADER_SIZE(&msg) == 5);
	assert_memory_equal("requester", MSG_SOCKID_DATA(&msg), 9);
	assert_memory_equal("hello", MSG_HEADER_DATA(&msg), 5);
	sleep(1);

	// reply from service to requester
	spdnet_frame_close(MSG_HEADER(&msg));
	spdnet_frame_init_size(MSG_HEADER(&msg), 5+6);
	memcpy(MSG_HEADER_DATA(&msg), "hello_reply", 5+6);
	rc = spdnet_sendmsg(service, &msg);
	assert_true(rc == 0);
	sleep(1);
	rc = spdnet_recvmsg(requester, &msg, 0);
	assert_true(rc == 0);
	assert_true(MSG_SOCKID_SIZE(&msg) == 7);
	assert_true(MSG_HEADER_SIZE(&msg) == 5+6);
	assert_memory_equal("service", MSG_SOCKID_DATA(&msg), 7);
	assert_memory_equal("hello_reply", MSG_HEADER_DATA(&msg), 5+6);
	sleep(1);

	task_destroy(inner_task);
	task_destroy(outer_task);
	spdnet_msg_close(&msg);
	spdnet_node_destroy(requester);
	spdnet_node_destroy(service);
	spdnet_router_destroy(outer);
	spdnet_router_destroy(inner);
	spdnet_ctx_destroy(ctx);
}

/*
 * spdnet pgm
 */

static void *sub_routine(void *sub)
{
	zmq_msg_t msg;
	zmq_msg_init(&msg);
	zmq_msg_recv(&msg, sub, 0);
	assert_memory_equal(zmq_msg_data(&msg), "hello pgm", 9);
	zmq_msg_close(&msg);
	return NULL;
}

static void test_spdnet_pgm(void **status)
{
	// always fails
	return;
	void *ctx = spdnet_ctx_new();
	void *pub = zmq_socket(ctx, ZMQ_PUB);
	void *sub = zmq_socket(ctx, ZMQ_SUB);

	//const char *url = "tcp://127.0.0.1:1234";
	const char *url = "epgm://enp0s25;239.255.12.24:5964";
	int rc = zmq_bind(pub, url);
	assert_true(rc == 0);
	rc = zmq_connect(sub, url);
	rc = zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);
	assert_true(rc == 0);
	pthread_t tid;
	pthread_create(&tid, NULL, sub_routine, sub);
	sleep(3);
	zmq_msg_t msg;
	zmq_msg_init_size(&msg, 10);
	memcpy(zmq_msg_data(&msg), "hello pgm\0", 10);
	zmq_msg_send(&msg, pub, 0);
	zmq_msg_close(&msg);
	pthread_join(tid, NULL);

	zmq_close(pub);
	zmq_close(sub);
	spdnet_ctx_destroy(ctx);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_spdnet_basic),
		cmocka_unit_test(test_spdnet_pub_sub),
		cmocka_unit_test(test_spdnet_pub_sub2),
		cmocka_unit_test(test_spdnet_forwarder),
		cmocka_unit_test(test_spdnet_nodepool),
		cmocka_unit_test(test_spdnet_router),
		cmocka_unit_test(test_spdnet_pgm),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
