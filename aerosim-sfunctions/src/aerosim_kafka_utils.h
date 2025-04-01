#include "mw_kafka_utils.h"
#include "mx_kafka_utils.h"

int aerosimInitializeKafkaConsumer(rd_kafka_t **prk,
    const char *brokers, const char *group, const char *topic,
    int confCount, int topicConfCount, const char **confArray,
    int64_t start_offset);