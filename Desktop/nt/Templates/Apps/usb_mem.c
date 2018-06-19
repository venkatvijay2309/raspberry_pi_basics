#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

#include "ddk_mem.h"

#define DEVICE "/dev/ddk_mem0"

#define READ_SIZE 80
#define WRITE_SIZE 80
#define BUF_SIZE (WRITE_SIZE + 1)

int main(int argc, char *argv[])
{
	char *filename = DEVICE;
	int fd;
	int choice;
	unsigned char data[BUF_SIZE];
	int to_read, read_cnt, cnt, i;
	off_t seek;
	int offset;

	if (argc > 2)
	{
		printf("Usage: %s [<device_file_name>]\n", argv[0]);
		return 1;
	}
	if (argc == 2)
	{
		filename = argv[1];
	}

	fd = open(filename, O_RDWR);
	if (fd == -1)
	{
		perror("usb_mem open");
		return 1;
	}

	do
	{
		printf(" 0: Exit\n");
		printf(" 1: Read %d bytes from DDK Memory\n", READ_SIZE);
		printf(" 2: Write (<= %d bytes) into DDK Memory\n", WRITE_SIZE);
		printf(" 3: Get current offset of DDK Memory\n");
		printf(" 4: Set current offset of DDK Memory\n");
		printf(" 5: Get size of DDK Memory\n");
		printf("Enter choice: ");
		scanf("%d", &choice);
		getchar();
		switch (choice)
		{
			case 1:
				to_read = READ_SIZE;
				cnt = 0;
				while (to_read)
				{
					read_cnt = read(fd, data + cnt, to_read);
					if (read_cnt == -1)
					{
						perror("usb_mem read");
						break;
					}
					to_read -= read_cnt;
					cnt += read_cnt;
				}
				printf(" Read %d bytes from memory: ", cnt);
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
			case 2:
				printf("Enter data (<= %d characters) for memory transfer: ", WRITE_SIZE);
				scanf("%[^\n]", data);
				getchar();
				cnt = write(fd, data, strlen(data));
				if (cnt == -1)
				{
					perror("usb_mem write");
					break;
				}
				printf("Wrote %d bytes\n", cnt);
				break;
			case 3:
				if ((offset = lseek(fd, 0, SEEK_CUR)) == -1)
				{
					perror("usb_mem lseek");
					break;
				}
				printf("Current FD Offset is 0x%x (%d)\n", offset, offset);
				break;

			case 4:
				printf("Enter the seek from start of memory: ");
				scanf("%lld", &seek);
				getchar();
				if (lseek(fd, seek, SEEK_SET) == -1)
				{
					perror("usb_mem lseek");
					break;
				}
				break;
			case 5:
				if (ioctl(fd, DDK_MEM_SIZE_GET, &offset) == -1)
				{
					perror("usb_mem ioctl");
					break;
				}
				printf("Memory Size is 0x%x (%d)\n", offset, offset);
				break;
			case 6:
				if (ioctl(fd, DDK_RD_OFF_GET, &offset) == -1)
				{
					perror("usb_mem ioctl");
					break;
				}
				printf("Current Memory Read Offset is 0x%x (%d)\n", offset, offset);
				break;
			case 7:
				if (ioctl(fd, DDK_WR_OFF_GET, &offset) == -1)
				{
					perror("usb_mem ioctl");
					break;
				}
				printf("Current Memory Write Offset is 0x%x (%d)\n", offset, offset);
				break;
		}
	} while (choice != 0);

	close (fd);
	return 0;
}
