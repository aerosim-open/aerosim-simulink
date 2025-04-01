#include "aerosim_kafka_utils.h"

static char errstr[512]; /* librdkafka API error reporting buffer */

/*
    This function is based on the mwInitializeKafkaConsumer() function
    in mw_kafka_utils.c with modified partition offset assignment to
    always start from the latest offset to bypass any stale data.
*/
int aerosimInitializeKafkaConsumer(rd_kafka_t **prk,
    const char *brokers, const char *group, const char *topic,
    int confCount, int topicConfCount, const char **confArray,
    int64_t start_offset)
{
    rd_kafka_t *rk = NULL;        /* Consumer instance handle */
    rd_kafka_conf_t *conf = NULL; /* Temporary configuration object */
    rd_kafka_topic_conf_t *topic_conf = NULL;
    rd_kafka_topic_partition_list_t *topics;
    int partition = 0;
    int i;
 
    conf = rd_kafka_conf_new();
    if (conf == NULL) {
        fprintf(stderr, "Couldn't instantiate Kafka config object.\n");
        return 1;
    }

    /* Topic configuration */
    topic_conf = rd_kafka_topic_conf_new();
    if (topic_conf == NULL) {
        fprintf(stderr, "Couldn't instantiate topic configuration\n");
        return 2;
    }

    if (rd_kafka_conf_set(conf, "group.id", group, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        fprintf(stderr, "%s\n", errstr);
        return 3;
    }

    rd_kafka_conf_set(conf, "enable.partition.eof", "true", NULL, 0);

    /* Set additional user defined configuration values */
    for (i = 0; i < confCount; i += 2) {
        char *key = (char *)confArray[i];
        char *value = (char *)(confArray[i + 1]);
        if (rd_kafka_conf_set(conf, key, value, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
            fprintf(stderr, "aerosimInitializeKafkaConsumer: %s\n", errstr);
            return 1;
        }
    }

    if (topicConfCount > 0) {
        /* Set additional user defined configuration values */
        for (i = 0; i < topicConfCount; i += 2) {
            char *key = (char *)confArray[confCount + i];
            char *value = (char *)(confArray[confCount + i + 1]);
            if (rd_kafka_topic_conf_set(topic_conf, key, value, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                fprintf(stderr, "aerosimInitializeKafkaConsumer: %s\n", errstr);
                return 2;
            }
        }
    }

    /* Set default topic config for pattern-matched topics. */
    /* This seems to make the app core dump */
    rd_kafka_conf_set_default_topic_conf(conf, topic_conf);

    /*
    * Create consumer instance.
    *
    * NOTE: rd_kafka_new() takes ownership of the conf object
    *       and the application must not reference it again after
    *       this call.
    */
    rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    if (!rk) {
        fprintf(stderr, "%s\n", errstr);
        return 4;
    }
    conf = NULL;

    /* Add brokers */
    if (rd_kafka_brokers_add(rk, brokers) == 0) {
        fprintf(stderr, "%% No valid brokers specified\n");
        return 5;
    }

    rd_kafka_poll_set_consumer(rk);

    topics = rd_kafka_topic_partition_list_new(1);

    /* Set initial configured offset to start_offset */
    rd_kafka_topic_partition_t* topic_part = rd_kafka_topic_partition_list_add(topics, topic, partition);
    topic_part->offset = start_offset;

    /* Try to fetch high watermarks for the partition to get the latest offset of any stale data */
    int64_t low_offset;
    int64_t high_offset;
    rd_kafka_resp_err_t err = rd_kafka_query_watermark_offsets(rk, topic, topic_part->partition, &low_offset, &high_offset, 5000);
    if (err) {
        fprintf(stderr, "%% Result of querying topic '%s' watermark offsets: %s\n", topic, rd_kafka_err2str(err));
    } else if (high_offset >=0) {
        topic_part->offset = high_offset;
    }
    fprintf(stderr, "%% Topic '%s' initial offset set to: %ld\n", topic, topic_part->offset);

    /* Assign topic partitions to start consuming them */
    {
        rd_kafka_resp_err_t err;
        if ((err = rd_kafka_assign(rk, topics))) {
            static char msg[256];

            fprintf(stderr, "%% Failed to start consuming topics: %s\n", rd_kafka_err2str(err));
            return 6;
        }
    }

    *prk = rk;
    return 0;
}