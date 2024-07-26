#include <librdkafka/rdkafka.h>

rd_kafka_conf_t *rd_kafka_conf_new(void)
{
    return NULL;
}

rd_kafka_conf_res_t rd_kafka_conf_set(rd_kafka_conf_t *conf, const char *name, const char *value, char *errstr, size_t errstr_size)
{
    return RD_KAFKA_CONF_OK;
}

rd_kafka_t *rd_kafka_new(rd_kafka_type_t type, rd_kafka_conf_t *conf, char *errstr, size_t errstr_size)
{
    return NULL;
}

rd_kafka_topic_t *rd_kafka_topic_new(rd_kafka_t *rk, const char *topic, rd_kafka_topic_conf_t *conf)
{
    return NULL;
}

int rd_kafka_produce(rd_kafka_topic_t *rkt, int32_t partition, int msgflags, void *payload, size_t len, const void *key, size_t keylen, void *msg_opaque)
{
    return 0;
}

rd_kafka_resp_err_t rd_kafka_last_error (void)
{
    return RD_KAFKA_RESP_ERR_NO_ERROR;
}

const char *rd_kafka_err2name (rd_kafka_resp_err_t err)
{
    return NULL;
}

const char *rd_kafka_err2str (rd_kafka_resp_err_t err)
{
    return NULL;
}