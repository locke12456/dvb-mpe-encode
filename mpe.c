#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>

#include <linux/if_tun.h>

#include "sectioncrc.h"

/* pre 2.4.6 compatibility */
#define OTUNSETNOCSUM  (('T'<< 8) | 200)
#define OTUNSETDEBUG   (('T'<< 8) | 201)
#define OTUNSETIFF     (('T'<< 8) | 202)
#define OTUNSETPERSIST (('T'<< 8) | 203)
#define OTUNSETOWNER   (('T'<< 8) | 204)

static int persist = 0;
static int stuff = 0;
static char padding[184];
static char ip_device[IFNAMSIZ];
static char s[180];
static const char *Id = "$Id$";
const MPE_HEADER_LEN=12;
int tun_fd = -1;

int tun_open(char *dev)
{
    struct ifreq ifr;
    int fd;

    if ((fd = open("/dev/net/tun", O_RDWR)) < 0)
    {
        perror("open tun");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN;
    if(persist == 0)
      ifr.ifr_flags |= IFF_NO_PI;
    if (*dev)
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    else
    {
        fprintf(stderr, "device name not supplied\n");
        return -1;
    }

    if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
        if (errno == EBADFD) {
            /* Try old ioctl */
            if (ioctl(fd, OTUNSETIFF, (void *) &ifr) < 0)
            {
                perror("new nor old ioctl worked\n");
                close(fd);
                return -1;
            }
        } else
	{
                perror("other error\n");
                close(fd);
                return -1;
	}
    }

    strncpy(dev, ifr.ifr_name, IFNAMSIZ);
    return fd;

}
FILE *ts = 0;  
void send_mpe(int fd, unsigned char *buf, size_t len)
{
    unsigned char *mpe_header = buf;
    unsigned char *ip_datagram = &buf[MPE_HEADER_LEN];
    unsigned long crc;
    unsigned long i;
    unsigned short section_len = len - 3 + 4; // table id and section len not in, crc in!
    mpe_header[0] = 0x3e;
    mpe_header[1] = ((section_len >> 8) & 0x0f) | 0xb0;
    mpe_header[2] = section_len & 0xff;
    mpe_header[3] = 0;
    mpe_header[4] = 0;
    mpe_header[5] = 0xc1;
    mpe_header[6] = 0;
    mpe_header[7] = 0;
    mpe_header[8] = 0;
    mpe_header[9] = 0;
    mpe_header[10] = 0;
    mpe_header[11] = 0;
    if ((ip_datagram[16] & 0xe0) == 0xe0) {	/* multicast */
        mpe_header[3] = ip_datagram[19];
        mpe_header[4] = ip_datagram[18];
        mpe_header[8] = ip_datagram[17] & 0x7f;
        mpe_header[9] = 0x5e;
        mpe_header[10] = 0;
        mpe_header[11] = 1;
    }
    crc = htonl(sectioncrc(buf, len));
    memcpy(&buf[len], &crc, 4);
    len += 4;
    write(fd, buf, len);
    fwrite(buf, 1, len, ts);
    if (stuff) {
        unsigned long stuff_count = 184 - (len % 184) - 1; /* one less for TS packet pointer */
        if (stuff_count > 0) {
            write(fd, padding, stuff_count);
			fwrite(padding, 1, stuff_count, ts);
        }
    }
}

void usage(char **argv)
{
    fprintf(stderr, "usage %s [-p] [-s] devname\n", argv[0]);
    fprintf(stderr,
            "Create a tun device and send DVB/MPE DSM-CC sections to stdout.\n");
    fprintf(stderr,
            "-p don't create or configure the tun device\n");
    fprintf(stderr,
            "-s stuff sections to multiple of 184 bytes using 0xff octets\n");
    fprintf(stderr,
            "Project home page http://code.google.com/p/dvb-mpe-encode\n");
    fprintf(stderr, "Example:\nmpe dvb0 | sec2ts 430 | DtPlay 1000000\n");
    exit(1);
}


void exit_program(int sig)
{
    fprintf(stderr, "stopping %s\n", ip_device);
    sprintf(s, "ifdown %s", ip_device);
    system(s);
    close(tun_fd);
    fclose(ts);
    exit(0);
}

int main(int argc, char **argv)
{
    int n = 1;
    if (argc < 2)
        usage(argv);
    if (argc > 4)
        usage(argv);
    while(argv[n][0] == '-')
    {
        if (strcmp(argv[n], "-p") == 0)
		persist = 1;
        if (strcmp(argv[n], "-s") == 0)
	{
		stuff = 1;
		memset(padding, 0xff, sizeof(padding));
	}
	n++;
    }
    strcpy(ip_device, argv[n]);
    tun_fd = tun_open (ip_device);
    if (tun_fd == -1)
        usage(argv);
    (void) signal(SIGINT, exit_program);
    (void) signal(SIGQUIT, exit_program);
    if(persist==0)
    {
      sprintf(s, "ifup %s", ip_device);
      system(s);
    }
    ts = fopen("test.ts", "a");  
    while (1) {
        unsigned char buf[4100];
        unsigned char *mpe_header = buf;
        unsigned char *tun_header = &buf[MPE_HEADER_LEN];
        int n = read(tun_fd, tun_header, sizeof(buf));
        //write(2, tun_header, n);
        send_mpe(1, mpe_header, n + MPE_HEADER_LEN);
    }
}
