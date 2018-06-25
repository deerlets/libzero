#include "spdnet.h"
#include <assert.h>

int spdnet_multicast_init(struct spdnet_multicast *mc,
                          const char *pgm_addr, int hops, void *ctx)
{
	int rc;
	void *sub, *pub;

	sub = zmq_socket(ctx, ZMQ_SUB);
	rc = zmq_setsockopt(sub, ZMQ_MULTICAST_HOPS, &hops, sizeof(int));
	assert(rc == 0);
	rc = zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);
	assert(rc == 0);
	spdnet_node_init_socket(&mc->sub, SPDNET_SUB, sub);
	rc = spdnet_connect(&mc->sub, pgm_addr);
	assert(rc == 0);

	pub = zmq_socket(ctx, ZMQ_PUB);
	rc = zmq_setsockopt(pub, ZMQ_MULTICAST_HOPS, &hops, sizeof(int));
	assert(rc == 0);
	spdnet_node_init_socket(&mc->pub, SPDNET_PUB, pub);
	rc = spdnet_bind(&mc->pub, pgm_addr);
	assert(rc == 0);

	return rc;
}

int spdnet_multicast_close(struct spdnet_multicast *mc)
{
	spdnet_node_close(&mc->sub);
	spdnet_node_close(&mc->pub);
	return 0;
}

int spdnet_multicast_recv(struct spdnet_multicast *mc,
                          struct spdnet_msg *msg, int flags)
{
	return spdnet_recvmsg(&mc->sub, msg, flags);
}

int spdnet_multicast_send(struct spdnet_multicast *mc, struct spdnet_msg *msg)
{
	return spdnet_sendmsg(&mc->pub, msg);
}
