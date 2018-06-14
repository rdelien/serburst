#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <linux/serial.h>
#include <poll.h>
#include <errno.h>
#include <stropts.h>
#include <asm/ioctls.h>

#include "termios_conv.h"


/*****************************************************************************/
/*** Macros								   ***/
/*****************************************************************************/
#define INTDIV(n, d)		(2*(n)+(d))/(2*(d))


/*****************************************************************************/
/*** Globals								   ***/
/*****************************************************************************/
/* Command line options */
static speed_t		bitRate		= 115200;
static char		ttyPort[32]	= "/dev/ttyUSB0";
static int		BeVerbose	= 0;
static int		Receiver	= 0;
static int		Transmitter	= 0;
static int		FlowControl	= 0;
static int		Restart		= 0;

static int		file		= 0;
struct serial_struct	oldserinfo;
static struct termios	oldtio;
static unsigned char	pattern[256];


/*****************************************************************************/
/*** Static functions							   ***/
/*****************************************************************************/
static int init(char const * const serport)
{
	int			oflag;
	int			standard_bitrate;
	int			c;
	struct termios		newtio;

	if (file)
		return -1;

	if (BeVerbose)
		printf("Opening %s\n", serport);

	/* Open the serial port */
	if (Receiver && Transmitter)
		oflag = O_RDWR;
	else if (Transmitter)
		oflag = O_WRONLY;
	else
		oflag = O_RDONLY;
	file = open(serport, oflag | O_NOCTTY );
	if (file < 0) {
		perror(serport);
		return -errno;
	}

	/* Store current serial configuration */
	if (ioctl(file, TIOCGSERIAL, &oldserinfo) < 0)
		perror("TIOCGSERIAL");

	/* Check if we need to set a custom bit rate */
	if (!(standard_bitrate = termios_bitrate(bitRate))) {
		struct serial_struct	newserinfo;

		/* Set custom bit rate */
		if (ioctl(file, TIOCGSERIAL, &newserinfo) < 0)
			perror("TIOCGSERIAL");
		newserinfo.flags = (newserinfo.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
		if ((newserinfo.custom_divisor = INTDIV(newserinfo.baud_base, bitRate)) < 1)
			newserinfo.custom_divisor = 1;
		if (ioctl(file, TIOCSSERIAL, &newserinfo) < 0)
			perror("TIOCSSERIAL");

		/* Verify custom bit rate */
		if (ioctl(file, TIOCGSERIAL, &newserinfo) < 0)
			perror("TIOCGSERIAL");
		if (newserinfo.custom_divisor * bitRate != newserinfo.baud_base)
			printf("Actual bit rate is %d / %d = %f\n", newserinfo.baud_base,
			       newserinfo.custom_divisor, (float)newserinfo.baud_base / newserinfo.custom_divisor);

		/* Set standard bit rate to custom bit rate marking value */
		standard_bitrate = B38400;
	}

	/* Save the current serial port settings */
	tcgetattr(file, &oldtio);

	/* Fill out the new serial port settings */
	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = standard_bitrate | CS8 | CLOCAL | CREAD | (FlowControl?CRTSCTS:0);
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0; /* Input mode (non-canonical, no echo,...) */
	newtio.c_cc[VTIME] = 0;   /* inter-character timer unused */
	newtio.c_cc[VMIN]  = 0;   /* No blocking read */

	/* Flush the buffer */
	tcflush (file, TCIFLUSH);

	/* Set the new attributes */
	tcsetattr (file, TCSANOW, &newtio);

	/* Initialize the pattern buffer */
	for (c = 0; c < 256; c++) {
#if defined TEST_CORRUPTED
		if (c == 0x51)
			pattern[c] = ~(unsigned char)c;
		else
			pattern[c] = (unsigned char)c;
#elif defined TEST_MISSING
		if (c >= 0x51)
			pattern[c] = (unsigned char)c + 1;
		else
			pattern[c] = (unsigned char)c;
#elif defined TEST_INSERTED
		if (c == 0x51)
			pattern[c] = ~(unsigned char)c;
		else if (c >= 0x51)
			pattern[c] = (unsigned char)c - 1;
		else
			pattern[c] = (unsigned char)c;
#else
		pattern[c] = (unsigned char)c;
#endif
	}

	return 0;
}


static void term(void)
{
	/* Restore the old attributes */
	tcsetattr(file,TCSANOW,&oldtio);

	/* Restore the old serial configuration */
	ioctl(file, TIOCSSERIAL, &oldserinfo);

	/* Flush the buffer */
	tcflush (file, TCIFLUSH);

	/* Close the serial port */
	close (file);

	file = 0;

	return;
}


static void transmit(void)
{
	static unsigned char	index = 0;
	unsigned int		txed;

	txed = write(file, &pattern[index], sizeof(pattern) - index);
/*
	if (BeVerbose && txed) {
		unsigned int count;
		printf("Transmitted %d bytes:", txed);
		for (count=0; count < txed; count++)
			printf(" 0x%.2X", pattern[index + count]);
		printf("\n");
	}
*/
	index += txed;
}


static void receive(void)
{
	static unsigned int		state		= 0;
	static unsigned char		read_byte[3]	= {-1, -1, -1};
	static unsigned long long	received	= 0;

	while (read(file, &read_byte[0], 1)) {
		received++;
		if (BeVerbose) {
			if (received == 1)
				printf("Receiving\n");
			else if (!(received & 0x3FFF))
				printf("%lld bytes\n", received);
		}

		if (BeVerbose > 1)
			printf("Rx: 0x%.2X\n", read_byte[0]);

		switch (state) {
		case 0:
			if (received < sizeof(read_byte))
				break;
			if (BeVerbose)
				printf("Verifying\n");
			state ++;
		case 1:
			if (read_byte[0] != (unsigned char)(read_byte[1] + 1)) {
				printf("Sync error: read 0x%.2X, expected 0x%.2X\n", read_byte[0], (unsigned char)(read_byte[1] + 1));
				state ++;
			}
			break;
		case 2:
			if (read_byte[0] == (unsigned char)(read_byte[2] + 1)) {
				printf("Resync: Error was caused by inserted 0x%.2X\n", read_byte[1]);
				state --;
			} else
			if (read_byte[0] == (unsigned char)(read_byte[2] + 2)) {
				printf("Resync: Error was caused by corrupted 0x%.2X\n", read_byte[1]);
				state --;
			} else
			if ((read_byte[0] == (unsigned char)(read_byte[1] + 1)) &&
			    (read_byte[0] == (unsigned char)(read_byte[2] + 3)) ) {
				printf("Resync: Error was caused by missing 0x%.2X\n", (unsigned char)(read_byte[1] - 1));
				state --;
			} else {
				printf("Unable to resync\n");
				printf("	Read:       0x%.2X\n", read_byte[0]);
				printf("	Read (t-1): 0x%.2X\n", read_byte[1]);
				printf("	Read (t-2): 0x%.2X\n", read_byte[2]);
				if (Restart) {
					printf("Restarting\n");
					state = 0;
					received = 0;
				} else {
					printf("Exiting\n");
					exit(0);
				}
			}
			break;
		}
		read_byte[2] = read_byte[1];
		read_byte[1] = read_byte[0];
	}
}


static void signal_callback_handler(int signum)
{
	printf("Caught signal %d\n",signum);

	term();

	exit(signum);
}


/*****************************************************************************/
/*** Functions								   ***/
/*****************************************************************************/
int main(int argc, char* argv[])
{
	int		arg;
	int		result = 0;
	struct pollfd	fds[1];

	signal(SIGINT, signal_callback_handler);

	/* Process the command line arguments */
	while ((arg = getopt(argc, argv, "b:fp:rtvh")) != -1) {
		switch (arg) {
		case 'b':
			/* Retrieve bit rate */
			bitRate = strtol(optarg, NULL, 0);
			break;
		case 'f':
			/* Enable flow control */
			FlowControl = 1;
			break;
		case 'p':
			/* Retrieve port */
			strncpy (ttyPort, optarg, sizeof(ttyPort));
			ttyPort[sizeof(ttyPort)-1] = 0;
			break;
		case 'r':
			/* Enable receiver */
			Receiver = 1;
			break;
		case 't':
			/* Enable transmitter */
			Transmitter = 1;
			break;
		case 'v':
			/* Be verbose */
			BeVerbose ++;
			break;
		default:
			result = -1;
		case 'h':
			fprintf(stderr, "Usage: %s\n", argv[0]);
			fprintf(stderr, "-b bit rate        Specify bitrate\n");
			fprintf(stderr, "-f                 Enable flow control\n");
			fprintf(stderr, "-p serial port     Specify serial port\n");
			fprintf(stderr, "-r                 Enable receiving\n");
			fprintf(stderr, "-t                 Enable transmitter\n");
			fprintf(stderr, "-v                 Be verbose\n");
			fprintf(stderr, "-h                 Show this help\n");
			return result;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc) {
		fprintf(stderr, "Stdin not supported\n");
		exit(-1);
	}

	if (init(ttyPort) < 0)
		exit(-1);

	memset(&fds[0], 0, sizeof(fds[0]));
	fds[0].fd = file;
	if (Receiver)
		fds[0].events |= POLLIN;
	if (Transmitter)
		fds[0].events |= POLLOUT;

	while (Transmitter || Receiver) {
		if (poll(fds, 1, -1) < 0) {
			perror("Polling serial port");
			result = -errno;
			break;
		}

		if (fds[0].revents & POLLOUT)
			transmit();

		if (fds[0].revents & POLLIN)
			receive();
	}

	term();

	return result;
}
