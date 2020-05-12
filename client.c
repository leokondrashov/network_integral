#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <math.h>
#include <poll.h>

#define DX 1e-7

struct connection {
	struct sockaddr_in addr;
	int sk;
	int n;
	int alive;
	int working;
};

struct arg {
	double from;
	double to;
	int worker;
	int done;
};

struct connection *setup_connections(int *count);

int main() {
	int count = 0;
	struct connection *conns = setup_connections(&count);

	struct pollfd *fds = calloc(count, sizeof(struct pollfd));

	int N = 0;
	for (int i = 0; i < count; i++) {
		N += conns[i].n;
		fds[i].fd = conns[i].sk;
		fds[i].events = POLLIN;
	}

	struct arg *args = calloc(N, sizeof(struct arg));
	
	args[0].from = 0.0;
	args[N - 1].to = 1.0;
	args[0].worker = -1;
	
	for (int i = 1; i < N; i++) {
		double x = floor(1.0 / N * i / DX) * DX;
//		printf("%.10g\n", x);
		args[i - 1].to = x;
		args[i].from = x;
		args[i].worker = -1;
	}

	int j = 0;
	for (int i = 0; i < count; i++) {
		if (conns[i].alive == 0)
			continue;
		
		for (int k = 0; k < conns[i].n; k++, j++) {
			send(conns[i].sk, &j, sizeof(int), 0);
			send(conns[i].sk, &(args[j].from), sizeof(double), 0);
			send(conns[i].sk, &(args[j].to), sizeof(double), 0);
			conns[i].working++;
			args[j].worker = i;
//			printf("%d:%g, %g\n", j, args[j].from, args[j].to);
		}
	}

	double res = 0;
	int done = 0;
	int alive = count;

	while (done < N && alive > 0) {
		if (poll(fds, count, -1) < 0) {
			perror("Error polling");
			return 1;
		}

		for (int i = 0; i < count; i++) {
//			printf("[%d]%x\n", i, fds[i].revents);
			if (fds[i].revents == 0)
				continue;

			if (fds[i].revents & POLLIN) {
				int num = -1;
				double cur = 0;
				if (recv(fds[i].fd, &cur, sizeof(double), 0) == 0)
					goto error;
					
				recv(fds[i].fd, &num, sizeof(int), 0);
				if (num == -1)
					goto error;
				
				res += cur;
//				printf("reading from %d\n", i);
//				printf("[%d]%d:%g\n", i, num, cur);
				args[num].done = 1;
				conns[i].working--;
				done++;
			}

			if ((fds[i].revents & POLLHUP) || (fds[i].revents & POLLERR) ||
			    (fds[i].revents & POLLRDHUP)) {
			error:
//				printf("%d is dead\n", i);
				for (int j = 0; j < N; j++) {
					if (!args[j].done && args[j].worker == i) {
						args[j].worker = -1;
					}
				}

				conns[i].alive = 0;
				conns[i].working = 0;
				fds[i].fd *= -1;
				alive--;
			}
			
			fds[i].revents = 0;
		}

		int j = 0;
		for (int i = 0; i < count; i++) {
			if (!conns[i].alive)
				continue;

			if (conns[i].working == conns[i].n)
				continue;

			while (args[j].worker != -1 && j < N)
				j++;

			if (j == N)
				break;

			for (int k = conns[i].working; k < conns[i].n; k++) {
				send(conns[i].sk, &j, sizeof(int), 0);
				send(conns[i].sk, &(args[j].from), sizeof(double), 0);
				send(conns[i].sk, &(args[j].to), sizeof(double), 0);
				conns[i].working++;
				args[j].worker = i;
//				printf("%d:%g, %g\n", j, args[j].from, args[j].to);
				while (args[j].worker != -1 && j < N)
					j++;

				if (j == N)
					break;
			}
		}
	}

	if (alive == 0) {
		printf("Zero servers left\n");
		exit(1);
	}

	printf("%g\n", res);
	
	for (int i = 0; i < count; i++)
		close(conns[i].sk);
	free(args);
	free(conns);
	free(fds);
}

void add_entry(struct connection **conns, int *count, int i) {
	if (i < *count)
		return;

	*conns = realloc(*conns, *count * 2 * sizeof(struct connection));
	memset(*conns + *count, 0, *count * sizeof(struct connection));
	*count *= 2;
}

struct connection *setup_connections(int *count) {
	int udp = socket(AF_INET, SOCK_DGRAM, 0);

	int bc = 1;
	if (setsockopt(udp, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc))) {
		perror("Failed to make broadcast socket");
		exit(1);
	}

	struct sockaddr_in broadcast_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(9009),
		.sin_addr.s_addr = INADDR_BROADCAST
	};	
	struct sockaddr_in server_addr = {};
//	struct sockaddr_in self = {};
	
	socklen_t len = sizeof(struct sockaddr_in);
	char request[] = "Request";

	char buf[50] = "";
	int buflen = 50;

	sendto(udp, request, strlen(request) + 1, 0,
	       (struct sockaddr *) &broadcast_addr, sizeof(broadcast_addr));

//	getsockname(udp, (struct sockaddr *) &self, &len);
//	printf("%s:%d\n", inet_ntoa(self.sin_addr), self.sin_port);

	fcntl(udp, F_SETFL, O_NONBLOCK);
	sleep(1);
        
	*count = 10;
	struct connection *conns = calloc(*count, sizeof(struct connection));
	int i = 0;
	while (recvfrom(udp, buf, buflen, 0, (struct sockaddr *) &server_addr, &len) > 0) {
//		printf("%s:%d\n", inet_ntoa(server_addr.sin_addr), server_addr.sin_port);
		if (strcmp(buf, "Response") != 0) {
			continue;
		}

		int sk = socket(AF_INET, SOCK_STREAM, 0);
		int res = connect(sk, (struct sockaddr *) &server_addr, sizeof(server_addr));
		if (res < 0) {
			close(sk);
			continue;
		}

		int turn_option_on = 1;
		int cnt = 5;
		int idle = 5;
		int intvl = 1;
		setsockopt(sk, SOL_SOCKET, SO_KEEPALIVE, &turn_option_on, sizeof(turn_option_on));
		setsockopt(sk, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
		setsockopt(sk, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
		setsockopt(sk, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
		add_entry(&conns, count, i);
		conns[i].addr = server_addr;
		conns[i].sk = sk;
		recv(conns[i].sk, &(conns[i].n), sizeof(int), 0);
		conns[i].alive = 1;
		i++;
	}
	
	close(udp);

	*count = i;
	return conns;
}
