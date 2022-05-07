/**
 * Hackathon SO: LogMemCacher
 * (c) 2020-2021, Operating Systems
 */

#define _GNU_SOURCE 1
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>
#include "../../include/server.h"

char *lmc_logfile_path;

/**
 * Client connection loop function. Creates the appropriate client connection
 * socket and receives commands from the client in a loop.
 *
 * @param client_sock: Socket to communicate with the client.
 *
 * @return: This function always returns 0.
 *
 * TODO: The lmc_get_command function executes blocking operations. The server
 * is unable to handle multiple connections simultaneously.
 */
static int
lmc_client_function(SOCKET client_sock)
{
	struct lmc_client *client;

	client = lmc_create_client(client_sock);

	while (1) {
		int rc = lmc_get_command(client);
		if (rc == -1)
			break;
	}

	close(client_sock);
	free(client);

	return 0;
}

/**
 * Server main loop function. Opens a socket in listening mode and waits for
 * connections.
 */
void
lmc_init_server_os(void)
{
	int sock, client_size, client_sock;
	struct sockaddr_in server, client;
	int opten;

	memset(&server, 0, sizeof(struct sockaddr_in));

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return;

	opten = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opten, sizeof(opten));

	server.sin_family = AF_INET;
	server.sin_port = htons(LMC_SERVER_PORT);
	server.sin_addr.s_addr = inet_addr(LMC_SERVER_IP);

	if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
		perror("Could not bind");
		exit(1);
	}

	if (listen(sock, LMC_DEFAULT_CLIENTS_NO) < 0) {
		perror("Error while listening");
		exit(1);
	}

	while (1) {
		memset(&client, 0, sizeof(struct sockaddr_in));
		client_size = sizeof(struct sockaddr_in);
		client_sock = accept(sock, (struct sockaddr *)&client,
				(socklen_t *)&client_size);

		if (client_sock < 0) {
			perror("Error while accepting clients");
		}

		lmc_client_function(client_sock);
	}
}

/**
 * OS-specific client cache initialization function.
 *
 * @param cache: Cache structure to initialize.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic.
 */
int
lmc_init_client_cache(struct lmc_cache *cache)
{
	int page_size = sysconf(_SC_PAGE_SIZE);
	char *client_file = malloc(sizeof(char) * FILENAME_MAX);
	sprintf(client_file,"%s/%s.log",lmc_logfile_path, cache->service_name);

	int fd = open(client_file, O_RDWR | O_CREAT);
	int file_pages;
	size_t file_size;

	if(fd < 0){
		perror("unexpected error opening file for logs");
		exit(-1);
	}

	file_size = lseek(fd,0,SEEK_END);
	lseek(fd,0,SEEK_SET);
	file_pages = file_size / page_size;
	if(file_size % page_size != 0){
		file_pages++;
	}
	if( file_pages < LMC_INIT_PAGENO * page_size)
		cache->pages = LMC_INIT_PAGENO;
	else
		cache->pages = file_pages + LMC_PAGE_INCREMENT;

	cache->ptr = mmap(NULL,cache->pages, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd,0);
	if(cache->ptr == MAP_FAILED){
		printf("error nr is %d",errno);
		perror("error mmap");
		exit(-1);
	}
	
	return 0;
}

/**
 * OS-specific function that handles adding a log line to the cache.
 *
 * @param client: Client connection;
 * @param log: Log line to add to the cache.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic. Must be able to dynamically resize the
 * cache if it is full.
 */
int
lmc_add_log_os(struct lmc_client *client, struct lmc_client_logline *log)
{
	int total_bytes = sizeof(struct lmc_client_logline)+client->cache->bytes_written;
	int page_size = sysconf(_SC_PAGE_SIZE);
	int pages_needed = total_bytes / page_size;
	if(total_bytes % page_size != 0){
		pages_needed++;
	}


	struct lmc_cache *cache = client->cache;
	void *noua_adresa;
	if(pages_needed>client->cache->pages){
		noua_adresa = mremap(client->cache->ptr, cache->bytes_written  + sizeof(struct lmc_client_logline), pages_needed, MREMAP_MAYMOVE);
	}
	client->cache->ptr = noua_adresa;
	memcpy(noua_adresa + client->cache->bytes_written , log, sizeof(struct lmc_client_logline));
	client->cache->bytes_written+=sizeof(struct lmc_client_logline);
	client->cache->log_number++;
	return 0;
}

/**
 * OS-specific function that handles flushing the cache to disk,
 *
 * @param client: Client connection.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic.
 */
int
lmc_flush_os(struct lmc_client *client)
{
	struct lmc_cache *cache = client->cache;	
	int ret = msync(cache->ptr, cache->bytes_written, MS_SYNC);
	if(ret == -1){
		perror("error syncronizing with log file");
		exit(-1);
	}
}

/**
 * OS-specific function that handles client unsubscribe requests.
 *
 * @param client: Client connection.
 *
 * @return: 0 in case of success, or -1 otherwise.
 *
 * TODO: Implement proper handling logic. Must flush the cache to disk and
 * deallocate any structures associated with the client.
 */
int
lmc_unsubscribe_os(struct lmc_client *client)
{
	struct lmc_cache *cache = client->cache;
	lmc_flush_os(client);
	int ret = munmap(cache->ptr, cache->bytes_written);
	if(ret < 0){
		perror("error unmapping");
		exit(-1);
	}
	free(cache->service_name);
	free(cache);
	close(client->client_sock);
	free(client);
}
