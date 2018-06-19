/* Serial Port Communication Program */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>

struct termios org_options;

void set_serial_options(int fd)
{
	struct termios options;

	tcgetattr(fd, &options);
	org_options = options;

	fcntl(fd, F_SETFL, 0);

#ifdef RAW_MODE
	cfmakeraw(&options);
#endif

	/* Input baud rate is set */
	cfsetispeed(&options, B9600);
	/* Output baud rate is set */
	cfsetospeed(&options, B9600);
	/* Reciever is enabled, so character can be recieved */
	options.c_cflag |= (CLOCAL | CREAD);
	options.c_cflag &= ~PARENB; // No parity
	/*
	 * Parity:
	 * options.c_cflag |= PARENB;
	 * 	Odd: options.c_cflag |= PARODD;
	 * 	Even: options.c_cflag &= ~PARODD;
	 */
	options.c_cflag &= ~CSTOPB; // 1 stop bit
	// options.c_cflag |= CSTOPB; // 2 stop bits
	options.c_cflag &= ~CSIZE;
	options.c_cflag |= CS8; // Can be from 5 to 8
	// No flow control
	options.c_cflag &= ~CRTSCTS;
	options.c_iflag &= ~(IXON | IXOFF);
	// options.c_cflag |= CRTSCTS; // Hardware flow control
	// options.c_iflag |= (IXON | IXOFF); // Software flow control
	options.c_lflag &= ~(ICANON | ECHO);
	options.c_oflag &= ~OPOST;
	/* Applying the new attributes */
	tcsetattr(fd, TCSANOW, &options);
}

void reset_serial_options(int fd)
{
	fcntl(fd, F_SETFL, 0);

	/* Applying the original attributes */
	tcsetattr(fd, TCSANOW, &org_options);
}

int main(int argc, char **argv)
{
	char *filename = "/dev/ttyS0";
	int fd;
	fd_set fds;
	int max_fds;
	char buf[80];
	int cnt;

	if (argc > 2)
	{
		fprintf(stderr, "Usage: %s [<device>]\n", argv[0]);
		return 1;
	}
	else if (argc == 2)
	{
		filename = argv[1];
	}

	if ((fd = open(filename, O_RDWR | O_NOCTTY)) < 0)
	{
		fprintf(stderr, "Error opening %s: %s\n", filename, strerror(errno));
		return 2;
	}

	set_serial_options(fd);

	FD_ZERO(&fds);
	FD_SET(0, &fds);
	FD_SET(fd, &fds);
	max_fds = fd + 1;

	while (select(max_fds, &fds, NULL, NULL, NULL) > 0)
	{
		if (FD_ISSET(0, &fds)) // Standard Input
		{
			cnt = read(0, buf, sizeof(buf));
			write(fd, buf, cnt);
		}
		if (FD_ISSET(fd, &fds)) // Serial Input
		{
			cnt = read(fd, buf, sizeof(buf));
			write(1, buf, cnt);
		}
		FD_SET(0, &fds);
		FD_SET(fd, &fds);
	}

	reset_serial_options(fd);
	
	close(fd);
	return 0;
}
