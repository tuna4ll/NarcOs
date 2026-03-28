#ifndef NET_H
#define NET_H

void net_init();
void net_poll();
int net_is_available();
int net_is_configured();
void net_print_status();
int net_run_dhcp(int verbose);
int net_dns_command(const char* host);
int net_ping_command(const char* target);

#endif
