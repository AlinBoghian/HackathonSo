/**
 * Hackathon SO: LogMemCacher
 * (c) 2020-2021, Operating Systems
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../include/server.h"

#ifdef __unix__
#include <sys/socket.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

static struct lmc_cache **lmc_caches;
static size_t lmc_cache_count;
static size_t lmc_max_caches;

/* Server API */

/**
 * Initialize client cache list on the server.
 */
static void
lmc_init_client_list(void)
{
	lmc_max_caches = LMC_DEFAULT_CLIENTS_NO;
	lmc_caches = malloc(lmc_max_caches * sizeof(*lmc_caches));
}

/**
 * Initialize server - allocate initial cache list and start listening on the
 * server's socket.
 */
static void
lmc_init_server(void)
{
	lmc_init_client_list();
	lmc_init_server_os();
}

/**
 * Create a client connection structure. Called to allocate the client
 * connection structure by the code that handles the server loop.
 *
 * @param client_sock: Socket used to communicate with the client.
 *
 * @return: A pointer to a client connection structure on success,
 *          or NULL otherwise.
 */
struct lmc_client *
lmc_create_client(SOCKET client_sock)
{
	struct lmc_client *client;

	client = malloc(sizeof(*client));
	client->client_sock = client_sock;
	client->cache = NULL;

	return client;
}

/**
 * Handle client connect.
 *
 * Locate a cache entry for the client and allot it to the client connection
 * (populate the cache field of the client connection structure).
 * If the client already has an existing connection (and respective cache) use
 * the same cache. Otherwise, locate a free cache.
 *
 * @param client: Client connection;
 * @param name: The name (identifier) of the client.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Must be able to handle the case when all caches are occupied.
 */
static int
lmc_add_client(struct lmc_client *client, char *name)
{
	printf("client attempting connect with name: %s",name);
	int err = 0;
	size_t i;

	for (i = 0; i < lmc_cache_count; i++) {
		if (lmc_caches[i] == NULL)
			continue;
		if (lmc_caches[i]->service_name == NULL)
			continue;
		if (strcmp(lmc_caches[i]->service_name, name) == 0) {
			client->cache = lmc_caches[i];
			goto found;
		}
	}

	if (lmc_cache_count == lmc_max_caches) {
		return -1;
	}

	client->cache = malloc(sizeof(*client->cache));
	client->cache->service_name = strdup(name);
	lmc_caches[lmc_cache_count] = client->cache;
	lmc_cache_count++;

	err = lmc_init_client_cache(client->cache);
found:
	printf("exiting add_client()");
	return err;
}

/**
 * Handle client disconnect.
 *
 * @param client: Client connection.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic.
 */
static int
lmc_disconnect_client(struct lmc_client *client)
{
	
	return 0;
}

/**
 * Handle unsubscription requests.
 *
 * @param client: Client connection.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic.
 */
static int
lmc_unsubscribe_client(struct lmc_client *client)
{
	int i;
	for (i = 0; i < lmc_cache_count; i++) {
		if (lmc_caches[i] == NULL)
			continue;
		if (lmc_caches[i]->service_name == NULL)
			continue;
		if (strcmp(lmc_caches[i]->service_name, client->cache->service_name) == 0) {
			lmc_caches[i] = lmc_caches[lmc_cache_count - 1];
			lmc_caches[lmc_cache_count - 1] = NULL;
			goto found;
		}
	}
	return -1;
found:
	return lmc_unsubscribe_os(client);
}
/**
 * Add a log line to the client's cache.
 *
 * @param client: Client connection;
 * @param log: Log line to add to the cache.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic.
 */
static int
lmc_add_log(struct lmc_client *client, struct lmc_client_logline *log)
{
	printf("adding log");
	return lmc_add_log_os(client, log);
}

/**
 * Flush client logs to disk.
 *
 * @param client: Client connection.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic.
 */
static int
lmc_flush(struct lmc_client *client)
{
	return lmc_flush_os(client);
}

/**
 * Send stats about the stored logs to the client. Must not send the actual
 * logs, but a string formatted in LMC_STATS_FORMAT. Should contain the current
 * time on the server, allocated memory in KBs and the number of log lines
 * stored.
 *
 * @param client: Client connection.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic.
 */
static int
lmc_send_stats(struct lmc_client *client)
{
	struct lmc_cache *cache = client->cache;
	long KBs = cache->bytes_written / 1024;
	char time[LMC_TIME_SIZE];
	char stats[LMC_STATUS_MAX_SIZE];

	lmc_crttime_to_str(time, LMC_TIME_SIZE, LMC_TIME_FORMAT);

	snprintf(stats,LMC_STATUS_MAX_SIZE, LMC_STATS_FORMAT, time, KBs, cache->log_number);
	int ret = lmc_send(client->client_sock, stats, LMC_LINE_SIZE,0);
	if(ret < 0)
		return -1;
	return 0;
}

/**
 * Send the stored log lines to the client.
 * The server must first send the number of lines, and then the log lines,
 * one by one.
 *
 * @param client: Client connection.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic.
 */
static int
lmc_send_loglines(struct lmc_client *client)
{
	char *buffer;
	buffer = (char *) malloc(LMC_LINE_SIZE);
	memset(buffer,0,LMC_LINE_SIZE);
	(*buffer) = client->cache->log_number;
	struct lmc_client_logline *log_vect =(struct lmc_client_logline *) client->cache->ptr;
	lmc_send(client->client_sock, buffer, LMC_LINE_SIZE, 0);
	for(int i = 0; i < client->cache->log_number; i++){
		lmc_send(client->client_sock, log_vect+i, LMC_LINE_SIZE, 0);
	}
	return 0;
}

/**
 * Parse a command from the client. The command must be in the following format:
 * "cmd data", with a single space between the command and the associated data.
 *
 * @param cmd: Parsed command structure;
 * @param string: Command string;
 * @param datalen: The amount of data send with the command.
 */
static void
lmc_parse_command(struct lmc_command *cmd, char *string, ssize_t *datalen)
{
	char *command, *line;

	command = strdup(string);
	line = strchr(command, ' ');

	cmd->data = NULL;
	if (line != NULL) {
		line[0] = '\0';
		cmd->data = strdup(line + 1);
		*datalen -= strlen(command) + 1;
	}

	cmd->op = lmc_get_op_by_str(command);

	printf("command = %s, line = %s\n", cmd->op->op_str,
			cmd->data ? cmd->data : "null");

	free(command);
}

/**
 * Validate command argument. The command argument (data) can only contain
 * printable ASCII characters.
 *
 * @param line: Command data;
 * @param len: Length of the data string.
 *
 * @return: 0 in case of success, or -1 otherwise.
 */
static int
lmc_validate_arg(const char *line, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		if (!isprint(line[i]))
			return -1;

	return 0;
}

/**
 * Wait for a command from the client and handle it when it is received.
 * The server performs blocking receive operations in this function. When the
 * command is received, parse it and then call the appropriate handling
 * function, depending on the command.
 *
 * @param client: Client connection.
 *
 * @return: 0 in case of success, or -1 otherwise.
 */
int
lmc_get_command(struct lmc_client *client)
{
	int err;
	ssize_t recv_size;
	char buffer[LMC_COMMAND_SIZE], response[LMC_LINE_SIZE];
	char *reply_msg;
	struct lmc_command cmd;
	struct lmc_client_logline *log;

	err = -1;

	memset(&cmd, 0, sizeof(cmd));
	memset(buffer, 0, sizeof(buffer));
	memset(response, 0, sizeof(response));

	recv_size = lmc_recv(client->client_sock, buffer, sizeof(buffer), 0);
	if (recv_size <= 0)
		return -1;

	lmc_parse_command(&cmd, buffer, &recv_size);
	if (recv_size > LMC_LINE_SIZE) {
		reply_msg = "message too long";
		goto end;
	}

	if (cmd.op->requires_auth && client->cache->service_name == NULL) {
		reply_msg = "authentication required";
		goto end;
	}

	if (cmd.data != NULL) {
		err = lmc_validate_arg(cmd.data, recv_size);
		if (err != 0) {
			reply_msg = "invalid argument provided";
			goto end;
		}
	}
	printf("op code %d\n",cmd.op->code);
	switch (cmd.op->code) {
	case LMC_CONNECT:
	case LMC_SUBSCRIBE:
		puts("about to call add_client");
		err = lmc_add_client(client, cmd.data);
		break;
	case LMC_STAT:
		err = lmc_send_stats(client);
		break;
	case LMC_ADD:
		puts("case lmc_add");
		/* TODO parse the client data and create a log line structure */
		log = malloc(sizeof(struct lmc_client_logline));
		memset(log,0,LMC_TIME_SIZE);
		memset(log,0,LMC_LOGLINE_SIZE);
		int time_size = strchr(cmd.data, '>') - cmd.data;
		memcpy(log->time,cmd.data,time_size);
		strcpy(log->logline,cmd.data + time_size);
		printf("about to call add log");
		err = lmc_add_log(client, log);
		break;
	case LMC_FLUSH:
		err = lmc_flush(client);
		break;
	case LMC_DISCONNECT:
		err = lmc_disconnect_client(client);
		break;
	case LMC_UNSUBSCRIBE:
		err = lmc_unsubscribe_client(client);
		break;
	case LMC_GETLOGS:
		err = lmc_send_loglines(client);
		break;
	default:
		/* unknown command */
		err = -1;
		break;
	}

	reply_msg = cmd.op->op_reply;

end:
	if (err == 0)
		sprintf(response, "%s", reply_msg);
	else
		sprintf(response, "FAILED: %s", reply_msg);

	if (cmd.data != NULL)
		free(cmd.data);
	puts("about to send response");
	return lmc_send(client->client_sock, response, LMC_LINE_SIZE,
			LMC_SEND_FLAGS);
}

int
main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	setvbuf(stdout, NULL, _IONBF,0);
	if (argc == 1)
		lmc_logfile_path = strdup("logs_lmc");
	else
		lmc_logfile_path = strdup(argv[1]);

	if (lmc_init_logdir(lmc_logfile_path) < 0)
		exit(-1);

	lmc_init_server();

	return 0;
}
