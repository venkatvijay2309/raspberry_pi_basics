#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

#include "led_ioctl.h"

int main(int argc, char *argv[])
{
	char *file_name = "/dev/led0";
	int fd;
	int choice;
	unsigned char data[128];
	int cnt, i;
	int delay;

	if (argc != 1)
	{
		fprintf(stderr, "Usage: %s\n", argv[0]);
		return 1;
	}
	fd = open(file_name, O_RDWR);
	if (fd == -1)
	{
		perror("led_ops open");
		return 1;
	}

	do
	{
		printf("0: Exit\n");
		printf("1: Read\n");
		printf("2: Write\n");
		printf("3: Ioctl\n");
		printf("Enter choice: ");
		scanf("%d", &choice);
		getchar();
		switch (choice)
		{
			case 1:
				printf("Bytes to read: ");
				scanf("%d", &cnt);
				getchar();
				cnt = read(fd, data, cnt);
				if (cnt == -1)
				{
					perror("led_ops read");
					break;
				}
				printf("Read %d bytes in %p: ", cnt, data);
				for (i = 0; i < cnt; i++)
				{
					printf("%c (%02X) ", data[i], data[i]);
				}
				printf("\n");
				break;
			case 2:
				printf("Enter your string: ");
				scanf("%[^\n]", data);
				getchar();
				cnt = write(fd, data, strlen(data));
				if (cnt == -1)
				{
					perror("led_ops write");
					break;
				}
				printf("Wrote %d bytes from %p\n", cnt, data);
				break;
			case 3:
				printf("Enter the delay in ms: ");
				scanf("%d", &delay);
				getchar();
				if (ioctl(fd, LED_SET_CHAR_DELAY, delay) == -1)
				{
					perror("led_ops ioctl");
					break;
				}
				break;
		}
	} while (choice != 0);

	close (fd);

	return 0;
}
