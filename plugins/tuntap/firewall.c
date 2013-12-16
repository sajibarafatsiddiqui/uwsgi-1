#include "common.h"

extern struct uwsgi_tuntap utt;

int uwsgi_tuntap_firewall_check(struct uwsgi_tuntap_firewall_rule *direction, char *pkt, uint16_t len) {
        // sanity check
        if (len < 20) return -1;
        uint32_t *src_ip = (uint32_t *) &pkt[12];
        uint32_t *dst_ip = (uint32_t *) &pkt[16];

        uint32_t src = ntohl(*src_ip);
        uint32_t dst = ntohl(*dst_ip);

        struct uwsgi_tuntap_firewall_rule *utfr = direction;
        while(utfr) {
                if (utfr->src) {
                        uint32_t src_masked = src & utfr->src_mask;
                        if (src_masked != utfr->src) goto next;
                }

                if (utfr->dst) {
                        uint32_t dst_masked = dst & utfr->dst_mask;
                        if (dst_masked != utfr->dst) goto next;
                }

                return utfr->action;
next:
                utfr = utfr->next;
        }

        return 0;
}

int uwsgi_tuntap_route_check(int fd, char *pkt, uint16_t len) {
        // sanity check
        if (len < 20) return -1;
        uint32_t *src_ip = (uint32_t *) &pkt[12];
        uint32_t *dst_ip = (uint32_t *) &pkt[16];

        uint32_t src = ntohl(*src_ip);
        uint32_t dst = ntohl(*dst_ip);

        struct uwsgi_tuntap_firewall_rule *utrr = utt.routes;
        while(utrr) {
                if (utrr->src) {
                        uint32_t src_masked = src & utrr->src_mask;
                        if (src_masked != utrr->src) goto next;
                }

                if (utrr->dst) {
                        uint32_t dst_masked = dst & utrr->dst_mask;
                        if (dst_masked != utrr->dst) goto next;
                }

		if (sendto(fd, pkt, len, 0, (struct sockaddr *) &utrr->dest_addr, utrr->addrlen) < 0) {
			uwsgi_error("uwsgi_tuntap_route_check()/sendto()");
		}
                return 1;
next:
                utrr = utrr->next;
        }

        return 0;
}


static struct uwsgi_tuntap_firewall_rule *uwsgi_tuntap_firewall_add_rule(struct uwsgi_tuntap_firewall_rule **direction, uint8_t action, uint32_t src, uint32_t src_mask, uint32_t dst, uint32_t dst_mask) {
#ifdef UWSGI_DEBUG
	uwsgi_log("[tuntap-router] firewall action %d %x %x %x %x\n",action, src,src_mask,dst,dst_mask);
#endif

	struct uwsgi_tuntap_firewall_rule *old_uttr = NULL, *uttr = *direction;
	while(uttr) {
		old_uttr = uttr;
		uttr = uttr->next;
	}

	uttr = uwsgi_calloc(sizeof(struct uwsgi_tuntap_firewall_rule));
	uttr->action = action;
	uttr->src = src & src_mask;
	uttr->src_mask = src_mask;
	uttr->dst = dst & dst_mask;
	uttr->dst_mask = dst_mask;

	if (old_uttr) {
		old_uttr->next = uttr;
	}
	else {
		*direction = uttr;
	}

	return uttr;
}

void uwsgi_tuntap_opt_firewall(char *opt, char *value, void *direction) {
	
	char *space = strchr(value, ' ');
	if (!space) {
		if (!strcmp("deny", value)) {
			uwsgi_tuntap_firewall_add_rule((struct uwsgi_tuntap_firewall_rule **) direction, 1, 0, 0, 0, 0);
			return;
		}
		uwsgi_tuntap_firewall_add_rule((struct uwsgi_tuntap_firewall_rule **) direction, 0, 0, 0, 0, 0);
		return;
	}

	*space = 0;

	uint8_t action = 0;
	if (!strcmp(value, "deny")) action = 1;

	char *space2 = strchr(space + 1, ' ');
	if (!space2) {
		uwsgi_log("invalid tuntap firewall rule syntax. must be <action> <src/mask> <dst/mask>");
	}

	*space2 = 0;

	uint32_t src = 0;
	uint32_t src_mask = 32;
	uint32_t dst = 0;
	uint32_t dst_mask = 32;

	char *slash = strchr(space + 1 , '/');
	if (slash) {
		src_mask = atoi(slash+1);
		*slash = 0;
	}

	if (inet_pton(AF_INET, space + 1, &src) != 1) {
		uwsgi_error("uwsgi_tuntap_opt_firewall()/inet_pton()");
		exit(1);
	}

	if (slash) *slash = '/';
	*space = ' ';

	slash = strchr(space2 + 1 , '/');
        if (slash) {
                dst_mask = atoi(slash+1);
                *slash = 0;
        }

        if (inet_pton(AF_INET, space2 + 1, &dst) != 1) {
                uwsgi_error("uwsgi_tuntap_opt_firewall()/inet_pton()");
                exit(1);
        }

        if (slash) *slash = '/';
        *space2 = ' ';

	uwsgi_tuntap_firewall_add_rule((struct uwsgi_tuntap_firewall_rule **) direction, action, ntohl(src), (0xffffffff << (32-src_mask)), ntohl(dst), (0xffffffff << (32-dst_mask)));
}

void uwsgi_tuntap_opt_route(char *opt, char *value, void *table) {

        char *space = strchr(value, ' ');
        if (!space) {
		uwsgi_log("invalid tuntap routing rule syntax, must be: <src/mask> <dst/mask> <gateway>\n");
		exit(1);
	}
        *space = 0;

        char *space2 = strchr(space + 1, ' ');
        if (!space2) {
		uwsgi_log("invalid tuntap routing rule syntax, must be: <src/mask> <dst/mask> <gateway>\n");
		exit(1);
        }
        *space2 = 0;

        uint32_t src = 0;
        uint32_t src_mask = 32;
        uint32_t dst = 0;
        uint32_t dst_mask = 32;

        char *slash = strchr(value , '/');
        if (slash) {
                src_mask = atoi(slash+1);
                *slash = 0;
        }
        if (inet_pton(AF_INET, value, &src) != 1) {
                uwsgi_error("uwsgi_tuntap_opt_route()/inet_pton()");
                exit(1);
        }
        if (slash) *slash = '/';

	slash = strchr(space+1 , '/');
        if (slash) {
                dst_mask = atoi(slash+1);
                *slash = 0;
        }
        if (inet_pton(AF_INET, space+1, &dst) != 1) {
                uwsgi_error("uwsgi_tuntap_opt_route()/inet_pton()");
                exit(1);
        }
        if (slash) *slash = '/';

	*space = ' ';
        *space2 = ' ';

        struct uwsgi_tuntap_firewall_rule * utfr = uwsgi_tuntap_firewall_add_rule((struct uwsgi_tuntap_firewall_rule **) table, 1, ntohl(src), (0xffffffff << (32-src_mask)), ntohl(dst), (0xffffffff << (32-dst_mask)));
	char *colon = strchr(space2+1, ':');
	if (!colon) {
		uwsgi_log("tuntap routing gateway must be a udp address in the form addr:port\n");
		exit(1);
	}
	utfr->dest_addr.sin_family = AF_INET;
	utfr->dest_addr.sin_port = htons(atoi(colon+1));
	*colon = 0;
	utfr->dest_addr.sin_addr.s_addr = inet_addr(space2+1);
	*colon = ':';
	utfr->addrlen = sizeof(struct sockaddr_in);
}

