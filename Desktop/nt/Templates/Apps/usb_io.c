#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>

#include "ddk_io.h"

#define DEVICE "/dev/ddk_io0"

/* PORT B */
#define BT 2
#define IR 3
#define DL 4

/* PORT D */
#define DATA 0
#define CLK 1

static char *reg_name[total_reg_id] =
{ "RSVD", "DIRA", "DIRB", "DIRC", "DIRD", "PORTA", "PORTB", "PORTC", "PORTD" };
static unsigned char reg_val[total_reg_id];

int read_reg_ind(void)
{
	RegId i, ind;

	printf("    ---------------\n");
	printf("    Register Index:\n");
	for (i = 0; i < total_reg_id; i++)
		printf("    %5s: %2d\n", reg_name[i], i);
	printf("    ---------------\n");
	printf("Enter register index: ");
	scanf("%u", &ind);
	getchar();
	if (ind >= total_reg_id)
	{
		printf("Invalid register index %d\n", ind);
		return -1;;
	}
	else
		return ind;
}

int main(int argc, char *argv[])
{
	char *filename = DEVICE;
	int fd;
	int choice;
	RegId ind;
	int val, i;
	Register r;

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
		perror("usb_io open");
		return 1;
	}

	do
	{
		printf(" 0: Exit\n");
		printf(" 1: Read from a DDK register\n");
		printf(" 2: Write into a DDK register\n");
		printf(" 3: Read BUTTON & DOWNLOAD switches\n");
		printf(" 4: Toggle IR LED\n");
		printf(" 5: Populate 8-LED Array\n");
		printf("Enter choice: ");
		scanf("%d", &choice);
		getchar();
		switch (choice)
		{
			case 1:
				if ((ind = read_reg_ind()) == -1)
					continue;
				r.id = ind;
				if (ioctl(fd, DDK_REG_GET, &r) == -1)
				{
					perror("usb_io ioctl");
					break;
				}
				reg_val[r.id] = r.val;
				printf("%s is 0x%02X\n", reg_name[r.id], reg_val[r.id]);
				break;
			case 2:
				if ((ind = read_reg_ind()) == -1)
					continue;
				r.id = ind;
				printf("Enter 8-bit register value in hex: ");
				scanf("%x", &val);
				getchar();
				r.val = val & 0xFF;
				if (ioctl(fd, DDK_REG_SET, &r) == -1)
				{
					perror("usb_io ioctl");
					break;
				}
				reg_val[r.id] = r.val; // Update only if the write was successful
				printf("%s written with 0x%02X\n", reg_name[r.id], reg_val[r.id]);
				break;
			case 3:
				r.id = portb;
				if (ioctl(fd, DDK_REG_GET, &r) == -1)
				{
					perror("usb_io ioctl");
					break;
				}
				reg_val[r.id] = r.val;
				printf("BUTTON is %s\n", (reg_val[r.id] & (1 << BT)) ? "RELEASED" : "PRESSED");
				printf("DOWNLOAD is %s\n", (reg_val[r.id] & (1 << DL)) ? "RELEASED" : "PRESSED");
				break;
			case 4:
				r.id = dirb;
				r.val = (reg_val[r.id] | (1 << IR));
				if (ioctl(fd, DDK_REG_SET, &r) == -1)
				{
					perror("usb_io ioctl");
					break;
				}
				reg_val[r.id] = r.val; // Update only if the write was successful
				r.id = portb;
				r.val = (reg_val[r.id] ^ (1 << IR));
				if (ioctl(fd, DDK_REG_SET, &r) == -1)
				{
					perror("usb_io ioctl");
					break;
				}
				reg_val[r.id] = r.val; // Update only if the write was successful
				printf("IR is %s\n", (reg_val[r.id] & (1 << IR)) ? "ON" : "OFF");
				break;
			case 5:
				r.id = dird;
				r.val = (reg_val[r.id] | (1 << DATA) | (1 << CLK));
				if (ioctl(fd, DDK_REG_SET, &r) == -1)
				{
					perror("usb_io ioctl");
					break;
				}
				reg_val[r.id] = r.val; // Update only if the write was successful
				printf("Enter 8-bit hex value for 8-LED Array: ");
				scanf("%x", &val);
				getchar();
				val &= 0xFF;
				r.id = portd;
				for (i = 7; i >= 0; i--)
				{
					r.val = reg_val[r.id];
					// Data is inverted
					if (val & (1 << i))
						r.val &= ~(1 << DATA);
					else
						r.val |= (1 << DATA);
					r.val &= ~(1 << CLK); // Clock low
					if (ioctl(fd, DDK_REG_SET, &r) == -1)
					{
						perror("usb_io ioctl");
						// Ignore the failure
						//break;
					}
					reg_val[r.id] = r.val; // Update only if the write was successful
					usleep(1); // 1 us
					r.val |= (1 << CLK); // Clock high
					if (ioctl(fd, DDK_REG_SET, &r) == -1)
					{
						perror("usb_io ioctl");
						// Ignore the failure
						//break;
					}
					reg_val[r.id] = r.val; // Update only if the write was successful
					usleep(1); // 1 us
				}
				r.id = dird;
				r.val = (reg_val[r.id] & ~((1 << DATA) | (1 << CLK)));
				if (ioctl(fd, DDK_REG_SET, &r) == -1)
				{
					perror("usb_io ioctl");
					break;
				}
				reg_val[r.id] = r.val; // Update only if the write was successful
				break;
			default:
				break;
		}
	} while (choice != 0);

	close (fd);
	return 0;
}
