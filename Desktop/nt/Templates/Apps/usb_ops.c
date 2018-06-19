#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

#include "ddk_led.h"

#define READ_SIZE 80
#define WRITE_SIZE 80
#define BUF_SIZE (WRITE_SIZE + 1)

int main(int argc, char *argv[])
{
	char *filename;
	int fd;
	int choice;
	unsigned char data[BUF_SIZE];
	int i, cnt;
	int led_val = 1;

	if (argc != 2)
	{
		printf("Usage: %s <device_file_name>\n", argv[0]);
		return 1;
	}
	else
	{
		filename = argv[1];
	}

	fd = open(filename, O_RDWR);
	if (fd == -1)
	{
		perror("usb_ops open");
		return 1;
	}

	do
	{
		printf(" 0: Exit\n");
		printf(" 1: Get LED Status from DDK\n");
		printf(" 2: Toggle LED Status of DDK\n");
		printf(" 3: Read %d bytes from DDK Serial\n", READ_SIZE);
		printf(" 4: Write (<= %d bytes) into DDK Serial\n", WRITE_SIZE);
		printf("Enter choice: ");
		scanf("%d", &choice);
		getchar();
		switch (choice)
		{
			case 1:
				if (ioctl(fd, DDK_LED_GET, &led_val) == -1)
				{
					perror("usb_ops ioctl");
					break;
				}
				printf(" LED is %s\n", (led_val == 0) ? "Off" : "On");
				break;
			case 2:
				led_val = !led_val;
				printf(" Switching %s LED ... ", (led_val == 0) ? "Off" : "On");
				if (ioctl(fd, DDK_LED_SET, led_val) == -1)
				{
					perror("usb_ops ioctl");
					printf("failed\n");
					break;
				}
				printf("done\n");
				break;
			case 3:
				cnt = read(fd, data, READ_SIZE);
				if (cnt == -1)
				{
					perror("usb_ops read");
					break;
				}
				printf(" Read %d bytes from serial: ", cnt);
				for (i = 0; i < cnt; i++)
				{
					printf("%c", data[i]);
				}
				printf("\n Hex:");
				for (i = 0; i < cnt; i++)
				{
					printf(" %02X", data[i]);
				}
				printf("\n");
				break;
			case 4:
				printf("Enter data (<= %d characters) for serial transfer: ", WRITE_SIZE);
				scanf("%[^\n]", data);
				getchar();
				cnt = write(fd, data, strlen(data));
				if (cnt == -1)
				{
					perror("usb_ops write");
					break;
				}
				printf("Wrote %d bytes\n", cnt);
				break;
		}
	} while (choice != 0);

	close (fd);
	return 0;
}
