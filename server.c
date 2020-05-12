#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>

#define DX 1e-9

struct arg {
	double from;
	double to;
	double res;
	int num;
};

int handleInt(const char *str, int *num);
int setup_connection();
void *threadRoutine(void *args);

int main(int argc, char **argv) {
	if (argc == 1) {
		printf("Missing argument\n");
		return 1;
	}
	
	int n = 0;
	if (handleInt(argv[1], &n)) {
		return 1;
	}

	int conn = setup_connection();

	send(conn, &n, sizeof(int), 0);

	struct arg *args = calloc(n, sizeof(struct arg));
 	pthread_t *threads = calloc(n, sizeof(pthread_t));

	int running = 0;
	char tmp = 0;
	do {
		if (recv(conn, &tmp, sizeof(char), MSG_PEEK) == 0)
			break;
		
		running = 0;
		for (int i = 0; i < n; i++) {
			if (recv(conn, &tmp, sizeof(char), MSG_PEEK | MSG_DONTWAIT) < 0)
				break;
			
			recv(conn, &(args[i].num), sizeof(int), 0);
			recv(conn, &(args[i].from), sizeof(double), 0);
			recv(conn, &(args[i].to), sizeof(double), 0);
//			printf("%d:%g, %g\n", args[i].num, args[i].from, args[i].to);
			if (pthread_create(&(threads[i]), NULL, threadRoutine, (void *)(args + i)) != 0) {
				printf("Failed to create threads\n");
				exit(1);
			}
			running++;
		}
//		exit(1);

		for (int i = 0; i < running; i++) {
			if (pthread_join(threads[i], NULL) != 0) {
				printf("Failed to join threads\n");
				return 1;
			}
			send(conn, &(args[i].res), sizeof(double), 0);
			send(conn, &(args[i].num), sizeof(int), 0);
//			printf("%d:%g\n", args[i].num, args[i].res);
//			exit(1);
		}
	} while (recv(conn, &tmp, sizeof(char), MSG_PEEK | MSG_DONTWAIT) != 0);

	free(threads);
	free(args);
	close(conn);
}


int handleInt(const char *str, int *num) {
	errno = 0;
	char *endp = NULL;
	*num = strtol(str, &endp, 10);
	if (errno == ERANGE) {
		printf("You've entered too big number. Unable to comply\n");
		return 1;
	}
	if (*num < 0) {
		printf("You've entered negative number. Unable to comply\n");
		return 1;
	}
	if (endp == str) {
		printf("You've entered nothing like number. Unable to comply\n");
		return 1;
	}
	if (*num == 0) {
		printf("You've entered 0. So?");
		return 1;
	}
	return 0;
}

int setup_connection() {
	
	struct sockaddr_in server_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(9009),
		.sin_addr.s_addr = INADDR_ANY
	};

	int udp = socket(AF_INET, SOCK_DGRAM, 0);

	int broadcast = 1;
	if (setsockopt(udp, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast))) {
		perror("Failed to make broadcast socket");
	        exit(1);
	}
	if (bind(udp, (struct sockaddr *) &server_addr, sizeof(server_addr))) {
		perror("Error binding");
	        exit(1);
	}

	socklen_t len = sizeof(struct sockaddr_in);
	char buf[50] = "";
	int buflen = 50;
	struct sockaddr_in client_addr = {};
	
	recvfrom(udp, buf, buflen, 0, (struct sockaddr *) &client_addr, &len);
	if (strcmp(buf, "Request") != 0)
		exit(0);

	char response[] = "Response";
	sendto(udp, response, strlen(response) + 1, 0,
	       (struct sockaddr *) &client_addr, sizeof(client_addr));
	
	close(udp);

	int tcp = socket(AF_INET, SOCK_STREAM, 0);
	int option = 1;
	setsockopt(tcp, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(int));
	fcntl(tcp, F_SETFL, O_NONBLOCK);
	if (bind(tcp, (struct sockaddr *) &server_addr, sizeof(server_addr))) {
		perror("Error binding");
		exit(1);
	}

	listen(tcp, 256);

	struct pollfd tcp_poll = {
		.fd = tcp,
		.events = POLLIN
	};
	
	if (poll(&tcp_poll, 1, 2000) == 0) {
		printf("Peer died\n");
		exit(1);
	}
	
	int conn = accept(tcp, (struct sockaddr *) &client_addr, &len);
	if (conn < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		printf("Peer died\n");
		exit(1);
	}
	
	close(tcp);

	return conn;
}

double f(double x) {
	return x;
}

void *threadRoutine(void *args) {
	assert(args);
	
	struct arg *arg = (struct arg *) args;

	double from = arg->from;
	double to = arg->to;
	
	double sum = 0;
	double fcur = f(from);
	double fprev = 0;
	for (double x = from; x <= to; x += DX) {
	        fprev = fcur;
		fcur = f(x);
		sum += (fprev + fcur) / 2 * DX;
	}
	arg->res = sum;

	return NULL;
}
