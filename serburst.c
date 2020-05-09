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
#include <stropts.h>
#include <asm/ioctls.h>
#include <sys/timerfd.h>
#include <setjmp.h>
#include <execinfo.h>

#include "termios_conv.h"


/*****************************************************************************/
/*** Macros                                                                ***/
/*****************************************************************************/
#define INTDIV(n, d)            ((n) + ((((n) >= 0 && (d) >= 0) || ((n) < 0 && (d) <0 )) ? ((d) / 2) : -((d) / 2))) / (d)

/* Macro to calculate the number of members in an array */
#define ARRAY_SIZE(arr)         (sizeof(arr) / sizeof((arr)[0]))


/*****************************************************************************/
/*** Globals                                                               ***/
/*****************************************************************************/
/* Command line options */
static speed_t               bit_rate     = 115200;
static char                  tty_name[32] = "/dev/ttyUSB0";
static int                   verbosity    = 0;
static int                   rx_enabled   = 0;
static int                   tx_enabled   = 0;
static int                   flow_control = 0;
static int                   restart      = 0;

static int                   ttyfd        = 0;
static struct serial_struct  oldserinfo;
static struct termios        oldtio;
static unsigned char         tty_set      = 0;

static unsigned char         pattern[256];

/* Back-trace data */
static void                  *bt_trace[20]; /* Back-trace buffer */
static size_t                bt_count;      /* Back-trace count */
static jmp_buf               bt_env;        /* Back-trace environment */


/*****************************************************************************/
/*** Static functions                                                      ***/
/*****************************************************************************/
static void transmit(unsigned long long *tx_bytes)
{
	static unsigned char  index = 0;
	ssize_t               bytes;

	if ((bytes = write(ttyfd, &pattern[index], sizeof(pattern) - index)) < 0) {
		perror("Writing data");
		return ;
	}
/*
	if (verbosity && *tx_bytes) {
		unsigned int count;
		fprintf(stdout, "Transmitted %d bytes:", *tx_bytes);
		for (count=0; count < *tx_bytes; count++)
			fprintf(stdout, " 0x%.2X", pattern[index + count]);
		fprintf(stdout, "\n");
	}
*/
	index     += bytes;
	*tx_bytes += bytes;
}


static void receive(unsigned long long *rx_bytes)
{
	static unsigned int   state        = 0;
	static unsigned char  read_byte[3] = {-1, -1, -1};
	ssize_t               bytes;

	while ((bytes = read(ttyfd, &read_byte[0], 1)) > 0) {
		*rx_bytes += bytes;
		if (verbosity) {
			if (*rx_bytes == 1)
				fprintf(stdout, "Receiving\n");
		}

		if (verbosity > 1)
			fprintf(stdout, "Rx: 0x%.2X\n", read_byte[0]);

		switch (state) {
		case 0:
			if (*rx_bytes < sizeof(read_byte))
				break;
			if (verbosity)
				fprintf(stdout, "Verifying\n");
			state ++;
		case 1:
			if (read_byte[0] != (unsigned char)(read_byte[1] + 1)) {
				fprintf(stderr, "Sync error: read 0x%.2X, expected 0x%.2X\n", read_byte[0], (unsigned char)(read_byte[1] + 1));
				state ++;
			}
			break;
		case 2:
			if (read_byte[0] == (unsigned char)(read_byte[2] + 1)) {
				fprintf(stderr, "Resync: Error was caused by inserted 0x%.2X\n", read_byte[1]);
				state --;
			} else if (read_byte[0] == (unsigned char)(read_byte[2] + 2)) {
				fprintf(stderr, "Resync: Error was caused by corrupted 0x%.2X\n", read_byte[1]);
				state --;
			} else if ((read_byte[0] == (unsigned char)(read_byte[1] + 1)) &&
			           (read_byte[0] == (unsigned char)(read_byte[2] + 3)) ) {
				fprintf(stderr, "Resync: Error was caused by missing 0x%.2X\n", (unsigned char)(read_byte[1] - 1));
				state --;
			} else {
				fprintf(stderr, "Unable to resync\n");
				fprintf(stderr, "	Read:       0x%.2X\n", read_byte[0]);
				fprintf(stderr, "	Read (t-1): 0x%.2X\n", read_byte[1]);
				fprintf(stderr, "	Read (t-2): 0x%.2X\n", read_byte[2]);
				if (restart) {
					fprintf(stderr, "Restarting\n");
					state = 0;
					*rx_bytes = 0;
				} else {
					fprintf(stderr, "Exiting\n");
					exit(0);
				}
			}
			break;
		}
		read_byte[2] = read_byte[1];
		read_byte[1] = read_byte[0];
	}

	if (bytes < 0)
		perror("Reading data");
}


static void set_tty(void)
{
	int             standard_bitrate;
	struct termios  newtio;

	/* Store current serial configuration */
	if (ioctl(ttyfd, TIOCGSERIAL, &oldserinfo) < 0)
		perror("TIOCGSERIAL");

	/* Check if we need to set a custom bit rate */
	if (!(standard_bitrate = termios_bitrate(bit_rate))) {
		struct serial_struct  newserinfo;

		/* Set custom bit rate */
		if (ioctl(ttyfd, TIOCGSERIAL, &newserinfo) < 0)
			perror("TIOCGSERIAL");
		newserinfo.flags = (newserinfo.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
		if ((newserinfo.custom_divisor = INTDIV(newserinfo.baud_base, bit_rate)) < 1)
			newserinfo.custom_divisor = 1;
		if (ioctl(ttyfd, TIOCSSERIAL, &newserinfo) < 0)
			perror("TIOCSSERIAL");

		/* Verify custom bit rate */
		if (ioctl(ttyfd, TIOCGSERIAL, &newserinfo) < 0)
			perror("TIOCGSERIAL");
		if (newserinfo.custom_divisor * bit_rate != newserinfo.baud_base)
			fprintf(stdout, "Actual bit rate is %d / %d = %f\n", newserinfo.baud_base,
			        newserinfo.custom_divisor, (float)newserinfo.baud_base / newserinfo.custom_divisor);

		/* Set standard bit rate to custom bit rate marking value */
		standard_bitrate = B38400;
	}

	/* Save the current serial port settings */
	tcgetattr(ttyfd, &oldtio);

	/* Fill out the new serial port settings */
	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = standard_bitrate | CS8 | CLOCAL | CREAD | (flow_control ? CRTSCTS : 0);
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0; /* Input mode (non-canonical, no echo,...) */
	newtio.c_cc[VTIME] = 0;   /* inter-character timer unused */
	newtio.c_cc[VMIN]  = 0;   /* No blocking read */

	/* Flush the buffer */
	tcflush(ttyfd, TCIFLUSH);

	/* Set the new attributes */
	tcsetattr (ttyfd, TCSANOW, &newtio);
}


static void restore_tty(void)
{
	/* Restore the old attributes */
	tcsetattr(ttyfd, TCSANOW, &oldtio);

	/* Restore the old serial configuration */
	ioctl(ttyfd, TIOCSSERIAL, &oldserinfo);

	/* Flush the buffer */
	tcflush(ttyfd, TCIFLUSH);
}


/*
 * This function installed as a signal handler, and is called automatically
 * whenever a SIGINT, SIGQUIT, SIGTERM or SIGSEGV is received. Its purpose is
 * to exit normally, enabling the terminator to shut down all subsystems.
 * Handling of SIGABRT and SIGSEGV are special cases. Because a back-trace is
 * desirable but printing is not recommended within a signal handler, the back-
 * trace is stored and a long jump to main is made, so it can be printed in
 * main context.
 */
static void signal_handler(int signum)
{
	switch (signum) {
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		/* Exit gracefully */
		if (tty_set)
			restore_tty();
		exit(signum);
		break;

	case SIGABRT:
	case SIGSEGV:
		/* Save the trace in statics */
		bt_count = backtrace(bt_trace, ARRAY_SIZE(bt_trace));

		/* Jump to main context */
		longjmp(bt_env, signum);
	}
}


/*****************************************************************************/
/*** Function                                                              ***/
/*****************************************************************************/
int main(int argc, char* argv[])
{
	int                 result = 0;
	int                 signum;
	int                 arg;
	int                 ndx;
	int                 oflag;
	struct pollfd       fds[2];
	int                 timerfd;
	struct itimerspec   timeout;
	unsigned long long  tx_bytes = 0;
	unsigned long long  rx_bytes = 0;

	/* Install signal handler */
	signal(SIGABRT, signal_handler);
	signal(SIGINT,  signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGPIPE, SIG_IGN); /* Ignore SIGPIPE */

	/* Create an environment copy to allow jumping here from signal handler, decoupling calls to syslog, sync et all */
	if ((signum = setjmp(bt_env)) != 0) {
		char    **strings = backtrace_symbols(bt_trace, bt_count);
		size_t  bt_ndx;

		fprintf(stderr, "Caught signal %d\n", signum);
		for (bt_ndx = 0; bt_ndx < bt_count; bt_ndx++)
			fprintf(stderr, "Back-trace: %s\n", strings[bt_ndx]);
		exit(EXIT_FAILURE);
	}

	/* Process the command line arguments */
	while ((arg = getopt(argc, argv, "b:fp:rtvh")) != -1) {
		switch (arg) {
		case 'b':
			/* Retrieve bit rate */
			bit_rate = strtol(optarg, NULL, 0);
			break;
		case 'f':
			/* Enable flow control */
			flow_control = 1;
			break;
		case 'p':
			/* Retrieve port */
			strncpy(tty_name, optarg, sizeof(tty_name));
			tty_name[sizeof(tty_name)-1] = 0;
			break;
		case 'r':
			/* Enable receiver */
			rx_enabled = 1;
			break;
		case 't':
			/* Enable transmitter */
			tx_enabled = 1;
			break;
		case 'v':
			/* Be verbose */
			verbosity++;
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
		result = EXIT_FAILURE;
		goto err_arg;
	}

	if (verbosity)
		fprintf(stdout, "Opening %s\n", tty_name);

	/* Open the serial port */
	if (rx_enabled && tx_enabled)
		oflag = O_RDWR;
	else if (tx_enabled)
		oflag = O_WRONLY;
	else
		oflag = O_RDONLY;

	if ((ttyfd = open(tty_name, oflag | O_NOCTTY)) < 0) {
		perror(tty_name);
		result = EXIT_FAILURE;
		goto err_ttyfd;
	}

	/* Initialize the pattern buffer */
	for (ndx = 0; ndx < 256; ndx++) {
#if defined TEST_CORRUPTED
		if (ndx == 0x51)
			pattern[ndx] = ~(unsigned char)ndx;
		else
			pattern[ndx] = (unsigned char)ndx;
#elif defined TEST_MISSING
		if (ndx >= 0x51)
			pattern[ndx] = (unsigned char)ndx + 1;
		else
			pattern[ndx] = (unsigned char)ndx;
#elif defined TEST_INSERTED
		if (ndx == 0x51)
			pattern[ndx] = ~(unsigned char)ndx;
		else if (ndx >= 0x51)
			pattern[ndx] = (unsigned char)ndx - 1;
		else
			pattern[ndx] = (unsigned char)ndx;
#else
		pattern[ndx] = (unsigned char)ndx;
#endif
	}

	set_tty();
	tty_set = 1;

	/* Create a new timer to report periodically */
	if ((timerfd = timerfd_create(CLOCK_MONOTONIC, 0)) < 0) {
		perror("Creating report timer");
		result = EXIT_FAILURE;
		goto err_timer;
	}

	/* Make timer periodic */
	memset(&timeout, 0, sizeof(timeout));
	timeout.it_value.tv_sec    = 1; /* Fire after 1 second */
	timeout.it_interval.tv_sec = 1; /* Repeat every second */
	if (timerfd_settime(timerfd, 0, &timeout, NULL) < 0) {
		perror("Setting report timeout");
		result = EXIT_FAILURE;
		goto err_timer_set;
	}

	memset(fds, 0, sizeof(fds));
	fds[0].fd = ttyfd;
	if (rx_enabled)
		fds[0].events |= POLLIN;
	if (tx_enabled)
		fds[0].events |= POLLOUT;
	fds[1].fd     = timerfd;
	fds[1].events = POLLIN;

	while (tx_enabled || rx_enabled) {
		if (poll(fds, ARRAY_SIZE(fds), -1) < 0) {
			perror("Polling serial port");
			result = EXIT_FAILURE;
			break;
		}

		if (fds[0].revents & POLLOUT)
			transmit(&tx_bytes);

		if (fds[0].revents & POLLIN)
			receive(&rx_bytes);

		/* Check if the report timeout time has expired */
		if (fds[1].revents & POLLIN) {
			static unsigned long long  tx_rate = 0;
			static unsigned long long  rx_rate = 0;
			unsigned long long         missed;

			if (read(fds[1].fd, &missed, sizeof(missed)) < 0) {
				perror("Error reading timer");
				result = EXIT_FAILURE;
				break;
			}

			fprintf(stdout, "Tx: %lld bytes (%lld kB/s), Rx: %lld bytes (%lld kB/s)\n",
			        tx_bytes, INTDIV(tx_bytes - tx_rate, 1024),
			        rx_bytes, INTDIV(rx_bytes - rx_rate, 1024));
			tx_rate = tx_bytes;
			rx_rate = rx_bytes;
		}
	}

err_timer_set:
	close(timerfd);
err_timer:
	restore_tty();
	/* Close the serial port */
	close(ttyfd);
err_ttyfd:
err_arg:
	return result;
}
