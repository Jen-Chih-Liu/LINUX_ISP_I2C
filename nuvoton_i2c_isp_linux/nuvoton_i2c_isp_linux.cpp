#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

#define I2C_Slave_ADDRESS 0x55
const char * devName = "/dev/i2c-1";
int file_i2c;

#define CMD_UPDATE_APROM	0x000000A0
#define CMD_UPDATE_CONFIG	0x000000A1
#define CMD_READ_CONFIG		0x000000A2
#define CMD_ERASE_ALL		0x000000A3
#define CMD_SYNC_PACKNO		0x000000A4
#define CMD_GET_FWVER		0x000000A6
#define CMD_APROM_SIZE		0x000000AA
#define CMD_RUN_APROM		0x000000AB
#define CMD_RUN_LDROM		0x000000AC
#define CMD_RESET			0x000000AD

#define CMD_GET_DEVICEID	0x000000B1

#define CMD_PROGRAM_WOERASE 	0x000000C2
#define CMD_PROGRAM_WERASE 	 	0x000000C3
#define CMD_READ_CHECKSUM 	 	0x000000C8
#define CMD_WRITE_CHECKSUM 	 	0x000000C9
#define CMD_GET_FLASHMODE 	 	0x000000CA

#define APROM_MODE	1
#define LDROM_MODE	2

#define BOOL  unsigned char
#define PAGE_SIZE                      0x00000200     /* Page size */

#define PACKET_SIZE	64
#define FILE_BUFFER	128
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif
unsigned char rcvbuf[PACKET_SIZE];
unsigned char sendbuf[PACKET_SIZE];
unsigned char aprom_buf[512];
unsigned int send_flag = FALSE;
unsigned int recv_flag = FALSE;
unsigned int g_packno = 1;
unsigned short gcksum;

unsigned short Checksum(unsigned char *buf, unsigned int len);
void WordsCpy(void *dest, void *src, unsigned int size);
BOOL CmdSyncPackno(int flag);
BOOL CmdGetCheckSum(int flag, int start, int len, unsigned short *cksum);
BOOL CmdGetDeviceID(int flag, unsigned int *devid);
BOOL CmdGetConfig(int flag, unsigned int *config);
BOOL CmdPutApromSize(int flag, unsigned int apsize);
BOOL CmdEraseAllChip(int flag);
BOOL CmdUpdateAprom(int flag);

#define dbg_printf printf
#define inpw(addr)            (*(unsigned int *)(addr))



//Copies the values of num bytes from the location pointed
//to by source directly to the memory block pointed to by destination.
void WordsCpy(void *dest, void *src, unsigned int size)
{
	unsigned char *pu8Src, *pu8Dest;
	unsigned int i;
    
	pu8Dest = (unsigned char *)dest;
	pu8Src  = (unsigned char *)src;
    
	for (i = 0; i < size; i++)
		pu8Dest[i] = pu8Src[i]; 
}
//Calculate the starting address and length of the incoming indicator, and return total checksum
unsigned short Checksum(unsigned char *buf, int len)
{
	int i;
	unsigned short c;

	for (c = 0, i = 0; i < len; i++) {
		c += buf[i];
	}
	return (c);
}
//ISP packet is 64 bytes for command packet sent
BOOL SendData(void)
{
	BOOL Result;
	int length;
	gcksum = Checksum(sendbuf, PACKET_SIZE);
	length = 64;			//<<< Number of bytes to write
	Result = TRUE;
	if (write(file_i2c, sendbuf, length) != length)		//write() returns the number of bytes actually written, if it doesn't match then an error occurred (e.g. no response from the device)
	{
		printf("length=%d\n\r", length);
		/* ERROR HANDLING: i2c transaction failed */
		printf("Failed to write to the i2c bus.\n");
		Result = FALSE;
	}
	return Result;
}
//ISP packet is 64 bytes for fixed ack packet received 
BOOL RcvData(unsigned int count)
{
	BOOL Result;
	int length;
	unsigned short lcksum, i;
	unsigned char *pBuf;
	length = 64;			//<<< Number of bytes to write
	if (read(file_i2c, rcvbuf, length) != length)		//write() returns the number of bytes actually written, if it doesn't match then an error occurred (e.g. no response from the device)
	{
		/* ERROR HANDLING: i2c transaction failed */
		printf("Failed to write to the i2c bus.\n");
	}
	pBuf = rcvbuf;
	WordsCpy(&lcksum, pBuf, 2);
	pBuf += 4;

	if (inpw(pBuf) != g_packno)
	{
		dbg_printf("g_packno=%d rcv %d\n", g_packno, inpw(pBuf));
		Result = FALSE;
	}
	else
	{
		if (lcksum != gcksum)
		{
			dbg_printf("gcksum=%x lcksum=%x\n", gcksum, lcksum);
			Result = FALSE;
		}
		g_packno++;
		Result = TRUE;
	}
	return Result;
}

//This command is used to synchronize packet number with ISP. 
//Before sending any command, master/host need send the command to synchronize packet number with ISP.
BOOL CmdSyncPackno(int flag)
{
	BOOL Result;
	unsigned long cmdData;
	printf("cmd sync\n\r");
	//sync send&recv packno
	memset(sendbuf, 0, PACKET_SIZE);
	cmdData = CMD_SYNC_PACKNO;//CMD_UPDATE_APROM
	WordsCpy(sendbuf + 0, &cmdData, 4);
	WordsCpy(sendbuf + 4, &g_packno, 4);
	WordsCpy(sendbuf + 8, &g_packno, 4);
	g_packno++;
	
	Result = SendData();
	if (Result == FALSE)
		return Result;

	Result = RcvData(1);
	
	return Result;
}

//This command is used to get version of ISP
BOOL CmdFWVersion(int flag, unsigned int *fwver)
{
	BOOL Result;
	unsigned long cmdData;
	unsigned int lfwver;
	
	//sync send&recv packno
	memset(sendbuf, 0, PACKET_SIZE);
	cmdData = CMD_GET_FWVER;
	WordsCpy(sendbuf + 0, &cmdData, 4);
	WordsCpy(sendbuf + 4, &g_packno, 4);
	g_packno++;
	
	Result = SendData();
	if (Result == FALSE)
		return Result;

	Result = RcvData(1);
	if (Result)
	{
		WordsCpy(&lfwver, rcvbuf + 8, 4);
		*fwver = lfwver;
	}
	
	return Result;
}

//This command is used to get product ID. 
//PC needs this ID to inquire size of APROM size and inform ISP.
BOOL CmdGetDeviceID(int flag, unsigned int *devid)
{
	BOOL Result;
	unsigned long cmdData;
	unsigned int ldevid;
	
	//sync send&recv packno
	memset(sendbuf, 0, PACKET_SIZE);
	cmdData = CMD_GET_DEVICEID;
	WordsCpy(sendbuf + 0, &cmdData, 4);
	WordsCpy(sendbuf + 4, &g_packno, 4);
	g_packno++;
	
	Result = SendData();
	if (Result == FALSE)
		return Result;

	Result = RcvData(1);
	if (Result)
	{
		WordsCpy(&ldevid, rcvbuf + 8, 4);
		*devid = ldevid;
	}
	
	return Result;
}
//This command is used to instruct ISP to read Config0 and Config1 information of flash memory, 
//and transmit them to host. 
BOOL CmdGetConfig(int flag, unsigned int *config)
{
	BOOL Result;
	unsigned long cmdData;
	unsigned int lconfig[2];
	
	//sync send&recv packno
	memset(sendbuf, 0, PACKET_SIZE);
	cmdData = CMD_READ_CONFIG;
	WordsCpy(sendbuf + 0, &cmdData, 4);
	WordsCpy(sendbuf + 4, &g_packno, 4);
	g_packno++;
	
	Result = SendData();
	if (Result == FALSE)
		return Result;

	Result = RcvData(1);
	if (Result)
	{
		WordsCpy(&lconfig[0], rcvbuf + 8, 4);
		WordsCpy(&lconfig[1], rcvbuf + 12, 4);
		config[0] = lconfig[0];
		config[1] = lconfig[1];
	}
	
	return Result;
}

//This command is used to instruct ISP to update Config0 and Config1.
BOOL CmdUpdateConfig(int flag, unsigned int *conf)
{
	BOOL Result;
	unsigned long cmdData;
	
	//sync send&recv packno
	memset(sendbuf, 0, PACKET_SIZE);
	cmdData = CMD_UPDATE_CONFIG;
	WordsCpy(sendbuf + 0, &cmdData, 4);
	WordsCpy(sendbuf + 4, &g_packno, 4);
	WordsCpy(sendbuf + 8, conf, 8);
	g_packno++;
	
	Result = SendData();
	if (Result == FALSE)
		return Result;

	Result = RcvData(2);
	
	return Result;
}

//for this commands
//CMD_RUN_APROM
//CMD_RUN_LDROM
//CMD_RESET
//CMD_ERASE_ALL
//CMD_GET_FLASHMODE
//CMD_WRITE_CHECKSUM
BOOL CmdRunCmd(unsigned int cmd, unsigned int *data)
{
	BOOL Result;
	unsigned int cmdData, i;
	
	//sync send&recv packno
	memset(sendbuf, 0, PACKET_SIZE);
	cmdData = cmd;
	WordsCpy(sendbuf + 0, &cmdData, 4);
	WordsCpy(sendbuf + 4, &g_packno, 4);
	if (cmd == CMD_WRITE_CHECKSUM)
	{
		WordsCpy(sendbuf + 8, &data[0], 4);
		WordsCpy(sendbuf + 12, &data[1], 4);
	}
	g_packno++;
	
	Result = SendData();
	if (Result == FALSE)
		return Result;
	
	if ((cmd == CMD_ERASE_ALL) || (cmd == CMD_GET_FLASHMODE) 
			|| (cmd == CMD_WRITE_CHECKSUM))
	{
		Result = RcvData(2);
		if (Result)
		{
			if (cmd == CMD_GET_FLASHMODE)
			{
				WordsCpy(&cmdData, rcvbuf + 8, 4);
				*data = cmdData;
			}
		}
		
	}
	else if ((cmd == CMD_RUN_APROM) || (cmd == CMD_RUN_LDROM)
		|| (cmd == CMD_RESET))
	{
		sleep(500);
	}
	return Result;
}
unsigned int file_totallen;
unsigned int file_checksum;

//the ISP flow, show to update the APROM in target chip 
BOOL CmdUpdateAprom(int flag)
{
	BOOL Result;
	unsigned int devid, config[2], i, mode, j;
	unsigned long cmdData, startaddr;
	unsigned short get_cksum;
	unsigned char Buff[256];
	unsigned int s1;
	FILE *fp;		//taget bin file pointer	

	g_packno = 1;
	
	//synchronize packet number with ISP. 
	Result = CmdSyncPackno(flag);
	if (Result == FALSE)
	{
		dbg_printf("send Sync Packno cmd fail\n");
		goto out1;
	}
	
#if 0
	//This command is used to get boot selection (BS) bit. 
	//If boot selection is APROM, the mode of returned is equal to 1,
	//Otherwise, if boot selection is LDROM, the mode of returned is equal to 2. 
	Result = CmdRunCmd(CMD_GET_FLASHMODE, &mode);
	if (mode != LDROM_MODE)
	{
		dbg_printf("fail\n");
		goto out1;
	}
	else
	{
		dbg_printf("ok\n");
	}
#endif
	//get product ID 
	CmdGetDeviceID(flag, &devid);
	printf("DeviceID: 0x%x\n", devid);
	
	//get config bit
	CmdGetConfig(flag, config);
	dbg_printf("config0: 0x%x\n", config[0]);
	dbg_printf("config1: 0x%x\n", config[1]);
	
	//open bin file for APROM
	if ((fp = fopen("//home//pi//test.bin", "rb")) == NULL)
	{
		printf("APROM FILE OPEN FALSE\n\r");
		Result = FALSE;
		goto out1;
	}
	
	//get BIN file size
	fseek(fp, 0, SEEK_END);
	file_totallen = ftell(fp);
	fseek(fp, 0, SEEK_SET);

    //first isp package
	memset(sendbuf, 0, PACKET_SIZE);
	cmdData = CMD_UPDATE_APROM;			//CMD_UPDATE_APROM Command
	WordsCpy(sendbuf + 0, &cmdData, 4);
	WordsCpy(sendbuf + 4, &g_packno, 4);
	g_packno++;
    
	//start address
	startaddr = 0;
	WordsCpy(sendbuf + 8, &startaddr, 4);
	WordsCpy(sendbuf + 12, &file_totallen, 4);
	
	fread(&sendbuf[16], sizeof(char), 48, fp);
	
	//send CMD_UPDATE_APROM
	Result = SendData();
	if (Result == FALSE)
		goto out1;
	printf("flash erase \n\r");
	//for erase time delay using, other bus need it.
	sleep(2);
	Result = RcvData(20);
	if (Result == FALSE)
		goto out1;
	printf("flash program \n\r");
	//Send other BIN file data in ISP package
	for (i = 48; i < file_totallen; i = i + 56)
	{
		dbg_printf("i=%d \n\r", i);

				//clear buffer
		for (j = 0; j < 64; j++)
		{
			sendbuf[j] = 0;
		}

		WordsCpy(sendbuf + 4, &g_packno, 4);
		g_packno++;
		if ((file_totallen - i) > 56)
		{			
			fread(&sendbuf[8], sizeof(char), 56, fp);
			Result = SendData();
			if (Result == FALSE)
				goto out1;
			usleep(100000);//program time
			Result = RcvData(2);
			if (Result == FALSE)
				goto out1;			
		}
		else
		{

			fread(&sendbuf[8], sizeof(char), file_totallen - i, fp);
			
			Result = SendData();
			if (Result == FALSE)
				goto out1;
			usleep(100000);//program time
			Result = RcvData(2);
			if (Result == FALSE)			
				goto out1;	
#if 0
			WordsCpy(&get_cksum, rcvbuf + 8, 2);
			if ((file_checksum & 0xffff) != get_cksum)	
			{			 
				Result = FALSE;
				goto out1;
			}
#endif
		}
	}

out1:
	return Result;
	
}


int main(int argc, char *argv[])
{
	clock_t start_time, end_time;
	float total_time = 0;
	start_time = clock();
	
	    // Open up the I2C bus
	file_i2c = open(devName, O_RDWR);
	if (file_i2c == -1)
	{
		printf("can't open\n\r");
		return (0);
	}
	printf("The I2C open\n\r");
	 // Specify the address of the slave device.
	if (ioctl(file_i2c, I2C_SLAVE, I2C_Slave_ADDRESS) < 0)
	{
		printf("Failed to acquire bus access and/or talk to slave");
		exit(0);
	}
	//ISP Updateflow
	CmdUpdateAprom(FALSE);
	
	
	close(file_i2c); //close i2c port
	end_time = clock();
	/* CLOCKS_PER_SEC is defined at time.h */
	total_time = (float)(end_time - start_time) / CLOCKS_PER_SEC;

	printf("\n\rTime : %f sec \n", total_time);
}