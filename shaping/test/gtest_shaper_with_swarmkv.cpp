#include <stdarg.h>
#include <sys/syscall.h>
#include <gtest/gtest.h>
#include <MESA/MESA_handle_logger.h>
#include <MESA/swarmkv.h>

#include "shaper_maat.h"
#include "stub.h"

struct swarmkv_cli_system
{
	int attached;
	char *cluster_name;
	char *attach_target_line;
};

typedef void proc_result_callback_t(struct cmd_exec_arg* exec_arg, void *uarg);
struct cmd_exec_arg
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int is_callback_executed;
	int success;
	int api_invoking_tid;
	pthread_t callback_invoking_tid;
	int is_sync_callback;
	struct swarmkv_reply expected_reply;
	proc_result_callback_t *cb;
	void *cb_uarg;
	int print_reply_on_fail;
	int diable_sync_check;
};

static struct swarmkv_cli_system g_cli_system;

int swarmkv_cli_create_cluster(const char *cluster_name, const char *node_string)
{
	char cmd_string[1024];
	snprintf(cmd_string, sizeof(cmd_string), "/opt/MESA/bin/swarmkv-cli --cluster-create %s %s", cluster_name, node_string);
	return system(cmd_string);
}

void swarmkv_cli_set_db(const char *cluster_name)
{
	if(g_cli_system.cluster_name) free(g_cli_system.cluster_name);
	asprintf(&(g_cli_system.cluster_name), "%s", cluster_name);
}

struct cmd_exec_arg* cmd_exec_arg_new(void)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)calloc(sizeof(struct cmd_exec_arg), 1);
	pthread_cond_init(&arg->cond, NULL);
	pthread_mutex_init(&arg->mutex, NULL);
	arg->callback_invoking_tid=syscall(SYS_gettid);
	return arg;
}

void cmd_exec_arg_expect_OK(struct cmd_exec_arg* arg)
{
	arg->expected_reply.type=SWARMKV_REPLY_STATUS;
	if(arg->expected_reply.str)
	{
		free(arg->expected_reply.str);
		arg->expected_reply.str=NULL;
	}
	arg->expected_reply.str=strdup("OK");
	arg->expected_reply.len=2;
}

void cmd_exec_arg_expect_cstring(struct cmd_exec_arg* arg, const char* string)
{
	arg->expected_reply.type=SWARMKV_REPLY_STRING;
	if(arg->expected_reply.str)
	{
		free(arg->expected_reply.str);
		arg->expected_reply.str=NULL;
	}
	arg->expected_reply.str=strdup(string);
	arg->expected_reply.len=strlen(string);

}

void cmd_exec_arg_clear(struct cmd_exec_arg* arg)
{
	arg->success=0;
	arg->is_callback_executed=0;
	if(!arg->diable_sync_check)
	{
		arg->api_invoking_tid = syscall(SYS_gettid);
	}
	arg->callback_invoking_tid=0;
	arg->is_sync_callback=0;
	return;
}

typedef void swarmkv_cli_reply_funt_t(struct cmd_exec_arg* reply_arg, const char *);

int swarmkv_cli_system_cmd(struct cmd_exec_arg* reply_arg, char *result, size_t result_len, swarmkv_cli_reply_funt_t *reply_funt_t, const char *format, ...)
{
	int line_num=0;
    char command[1024] = {0};
	char *cmd_str=NULL;

    va_list ap;
    va_start(ap,format);
	vasprintf(&cmd_str, format, ap);
    va_end(ap);

	if(g_cli_system.attached)
	{
		snprintf(command, sizeof(command), "/opt/MESA/bin/swarmkv-cli -n %s %s --exec %s", g_cli_system.cluster_name, g_cli_system.attach_target_line, cmd_str);
	}
	else
	{
		snprintf(command, sizeof(command), "/opt/MESA/bin/swarmkv-cli -n %s --exec %s", g_cli_system.cluster_name, cmd_str);
	}

	char *p=result;
	char line[1024] = {0};
    FILE *fp = NULL;

    if((fp = popen(command, "r")) == NULL)
	{
        printf("popen error!\n");
        return 0;
    }
	memset(result, 0, result_len);
    while (fgets(line, sizeof(line), fp))
	{
		int len= strlen(line);
		line[len-1]='\0';
		if((p - result) < (int)result_len)
		{
			if(line_num)
			{
				p += snprintf(p, result_len - (p - result), ",");
			}

			p += snprintf(p, result_len - (p - result), "%s", line);
		}
		line_num++;
    }
    pclose(fp);

	if(reply_funt_t)
		reply_funt_t(reply_arg, result);
	free(cmd_str);
	return 1;
}

void swarmkv_expect_reply_string(struct cmd_exec_arg* reply_arg, const char *line)
{
	EXPECT_STREQ(line, reply_arg->expected_reply.str);
}

static void send_packets(struct shaping_thread_ctx *ctx, struct shaping_flow *sf, int pkt_len, enum shaping_packet_dir dir, int is_tcp_pure_control)
{
    struct stub_packet *packet = (struct stub_packet *)calloc(1, sizeof(struct stub_packet));
    struct metadata meta;

    packet->direction = dir;
    packet->length = pkt_len;
    packet->flow = sf;

    memset(&meta, 0, sizeof(meta));

    meta.dir = dir;
    meta.raw_len = pkt_len;
    if (is_tcp_pure_control) {
        meta.is_tcp_pure_ctrl = 1;
    }

    shaping_packet_process(ctx, packet, &meta, sf);

    polling_entry(ctx->sp, ctx->stat, ctx);


    return;
}

TEST(generic_profile, single_session)
{
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf = NULL;
    const char *rule_uuid_strs[] = {"00000000-0000-0000-0000-000000000001"};
    int priority[] = {1};
    int profile_num[] = {1};
    const char *profile_uuid_strs[][MAX_REF_PROFILE] = {{ "00000000-0000-0000-0000-000000000001"}};
    struct cmd_exec_arg* reply_arg=NULL;
    char result[2048]={0};

    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf != NULL);

    stub_set_profile_limit_direction(profile_uuid_strs[0][0], PROFILE_LIMIT_DIRECTION_INCOMING_OUTGOING);
    stub_set_matched_shaping_rules(1, rule_uuid_strs, priority, profile_num, profile_uuid_strs);

    uuid_t rule_uuid;
    uuid_parse(rule_uuid_strs[0], rule_uuid);
    shaper_rules_update(&ctx->thread_ctx[0], sf, &rule_uuid, 1);

    //set swarmkv key
    swarmkv_cli_set_db("swarmkv-shaping-nodes");
    reply_arg=cmd_exec_arg_new();

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "tcfg tsg-shaping-00000000-0000-0000-0000-000000000001-incoming 1000000 1000000");
    swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "tcfg tsg-shaping-00000000-0000-0000-0000-000000000001-outgoing 1000000 1000000");
	cmd_exec_arg_clear(reply_arg);

    actual_tx_queue = stub_get_tx_queue();

    time_t start_time = time(NULL);
    unsigned long long total_bytes = 0;
    while (1) {
        if (time(NULL) - start_time >= 10) {
            break;
        }
        send_packets(&ctx->thread_ctx[0], sf, 100, SHAPING_DIR_IN, 0);
        while(!TAILQ_EMPTY(actual_tx_queue))
        {
            struct stub_packet_node *pkt_node = TAILQ_FIRST(actual_tx_queue);
            TAILQ_REMOVE(actual_tx_queue, pkt_node, node);
            total_bytes += pkt_node->raw_packet->length;
            free(pkt_node->raw_packet);
            free(pkt_node);
        }
    }

    EXPECT_GE(total_bytes * 8, 1000000 * 10);
    EXPECT_LE(total_bytes * 8, 1000000 * 11);

    while (shaper_global_stat_queueing_pkts_get() != 0) {
        polling_entry(ctx->thread_ctx[0].sp, ctx->stat, &ctx->thread_ctx[0]);
    }
    while(!TAILQ_EMPTY(actual_tx_queue))
    {
        struct stub_packet_node *pkt_node = TAILQ_FIRST(actual_tx_queue);
        TAILQ_REMOVE(actual_tx_queue, pkt_node, node);
        free(pkt_node->raw_packet);
        free(pkt_node);
    }
    

    shaping_flow_free(&ctx->thread_ctx[0], sf);
    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
    stub_clear_resource();
}

TEST(fair_share_profile, two_members)
{
    struct stub_pkt_queue *actual_tx_queue;
    struct shaping_ctx *ctx = NULL;
    struct shaping_flow *sf1 = NULL;
    struct shaping_flow *sf2 = NULL;
    const char *rule_uuid_strs[] = {"00000000-0000-0000-0000-000000000001", "00000000-0000-0000-0000-000000000002"};
    int priority[] = {1, 1};
    int profile_num[] = {1, 1};
    const char *profile_uuid_strs[][MAX_REF_PROFILE] = {{ "00000000-0000-0000-0000-000000000001"}, { "00000000-0000-0000-0000-000000000001"}};
    struct cmd_exec_arg* reply_arg=NULL;
    char result[2048]={0};

    stub_init();
    ctx = shaping_engine_init();
    ASSERT_TRUE(ctx != NULL);
    sf1 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf1 != NULL);
    sf2 = shaping_flow_new(&ctx->thread_ctx[0]);
    ASSERT_TRUE(sf2 != NULL);

    stub_set_matched_shaping_rules(2, rule_uuid_strs, priority, profile_num, profile_uuid_strs);
    stub_set_profile_limit_direction(profile_uuid_strs[0][0], PROFILE_LIMIT_DIRECTION_INCOMING_OUTGOING);
    stub_set_profile_type(profile_uuid_strs[0][0], PROFILE_TYPE_MAX_MIN_HOST_FAIRNESS);
    stub_set_shaping_rule_fair_factor(rule_uuid_strs[0], 1);
    stub_set_shaping_rule_fair_factor(rule_uuid_strs[1], 3);

    uuid_t rule_uuid1;
    uuid_t rule_uuid2;
    uuid_parse(rule_uuid_strs[0], rule_uuid1);
    uuid_parse(rule_uuid_strs[1], rule_uuid2);
    shaper_rules_update(&ctx->thread_ctx[0], sf1, &rule_uuid1, 1);
    shaper_rules_update(&ctx->thread_ctx[0], sf2, &rule_uuid2, 1);

    sf1->src_ip_str = (char *)calloc(1, 16);
    sf1->src_ip_str_len = strlen("1.1.1.1");
    memcpy(sf1->src_ip_str, "1.1.1.1", sf1->src_ip_str_len);
    
    sf2->src_ip_str = (char *)calloc(1, 16);
    sf2->src_ip_str_len = strlen("2.2.2.2");
    memcpy(sf2->src_ip_str, "2.2.2.2", sf2->src_ip_str_len);

    //set swarmkv key
    swarmkv_cli_set_db("swarmkv-shaping-nodes");
    reply_arg=cmd_exec_arg_new();

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "ftcfg tsg-shaping-00000000-0000-0000-0000-000000000001-incoming 1000000 1000000 256");
    swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "ftcfg tsg-shaping-00000000-0000-0000-0000-000000000001-outgoing 1000000 1000000 256");
	cmd_exec_arg_clear(reply_arg);

    actual_tx_queue = stub_get_tx_queue();

    time_t start_time = time(NULL);
    time_t last_time = start_time;
    unsigned long long total_bytes1 = 0;
    unsigned long long total_bytes2 = 0;
    while (1) {
        time_t curr_time = time(NULL);
        if (curr_time - last_time >= 1) {
            EXPECT_NEAR(total_bytes1 * 3, total_bytes2, 100);

            last_time = curr_time;
            total_bytes1 = 0;
            total_bytes2 = 0;
        }
        if (curr_time - start_time >= 10) {
            break;
        }
        send_packets(&ctx->thread_ctx[0], sf2, 100, SHAPING_DIR_IN, 0);
        send_packets(&ctx->thread_ctx[0], sf1, 100, SHAPING_DIR_IN, 0);
        while(!TAILQ_EMPTY(actual_tx_queue))
        {
            struct stub_packet_node *pkt_node = TAILQ_FIRST(actual_tx_queue);
            TAILQ_REMOVE(actual_tx_queue, pkt_node, node);
            if (pkt_node->raw_packet->flow == sf1) {
                total_bytes1 += pkt_node->raw_packet->length;
            } else {
                total_bytes2 += pkt_node->raw_packet->length;
            }
            free(pkt_node->raw_packet);
            free(pkt_node);
        }
    }

    while (shaper_global_stat_queueing_pkts_get() != 0) {
        polling_entry(ctx->thread_ctx[0].sp, ctx->stat, &ctx->thread_ctx[0]);
    }
    while(!TAILQ_EMPTY(actual_tx_queue))
    {
        struct stub_packet_node *pkt_node = TAILQ_FIRST(actual_tx_queue);
        TAILQ_REMOVE(actual_tx_queue, pkt_node, node);
        free(pkt_node->raw_packet);
        free(pkt_node);
    }

    shaping_flow_free(&ctx->thread_ctx[0], sf1);
    shaping_flow_free(&ctx->thread_ctx[0], sf2);
    shaper_thread_resource_clear();
    shaping_engine_destroy(ctx);
}

int main(int argc, char **argv) 
{
    testing::InitGoogleTest(&argc, argv);
    //testing::GTEST_FLAG(filter) = "single_session.udp_tx_in_order";

    const char *cluster_name="swarmkv-shaping-nodes";

    system("consul agent -dev -config-dir=/etc/consul.d > /dev/null 2>&1 &");
    sleep(3);

    swarmkv_cli_create_cluster(cluster_name, "127.0.0.1:5210");

    if (0 != MESA_handle_runtime_log_creation("./conf/zlog.conf"))
    {
        fprintf(stderr, "FATAL: unable to create runtime logger\n");
        return -1;
    }

    void *log_handle = MESA_create_runtime_log_handle("log/shaping", RLOG_LV_DEBUG);
    if (log_handle == NULL)
    {
        fprintf(stderr, "FATAL: unable to create log handle\n");
        return -1;
    }

    int ret = RUN_ALL_TESTS();

    MESA_destroy_runtime_log_handle(log_handle);
    MESA_handle_runtime_log_destruction();

    system("pkill -f consul");

    return ret;
}