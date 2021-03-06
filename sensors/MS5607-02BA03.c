/*
    User space program to read pressure and temperature from the 
    Measurement Specialties MS5607-02BA03 sensor available on the
    Moitessier HAT.
    This source code is for demonstation purpose only and was tested
    on a Raspberry Pi 3 Model B.
    
    Copyright (C) 2018  Thomas POMS <hwsw.development@gmail.com>
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
    Compiling
    =========
    
    arm-linux-gnueabihf-gcc -Wall MS5607-02BA03.c -o MS5607-02BA03
    
    Usage
    =====
    
    Running in endless loop:
    ./MS5607-02BA03 /dev/i2c-1
    
    Running with specified iterations:
    ./MS5607-02BA03 /dev/i2c-1 <ITERATIONS> <HUMAN_READABLE>
*/

#include <stdio.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <inttypes.h>

#define I2C_ADDR                    0x77                /* slave address of the sensor */
//#define I2C_BUS                     "/dev/i2c-1"        /* I2C bus where the sensor is connected to */
char* I2C_BUS;

/* sensor commands */
/* YOU MUST NOT USE CLOCK STRETCHING COMMANDS ON THE RASPBERRY PI */
#define CMD_D1_OSR_4096             0x48                /* convert digital pressure value */
#define CMD_D2_OSR_4096             0x58                /* convert digital temperature value */
#define CMD_READ_ADC                0x00
#define CMD_READ_PROM               0xA0

/* measure temperature */
int readPressure(int fd, uint16_t *prom, double *pressure, double *temp, uint32_t *d_D1, uint32_t *d_D2, int32_t *d_dT, int64_t *d_OFF, int64_t *d_SENS)
{
    uint8_t cmd;
    uint32_t D1 = 0, D2 = 0;
    double P, T;
    int64_t OFF, SENS;
    int32_t dT;
    uint8_t buf[4];
    
    /* initiate a pressure conversion */    
    cmd = CMD_D1_OSR_4096;
    if(write(fd, &cmd, 1) == -1)
        return -1;
    
    /* we need to wait till conversion has finished */    
    usleep(10 * 10000);
    
    /* read measurement result */
    cmd = CMD_READ_ADC;
    if(write(fd, &cmd, 1) == -1)
        return -1;
  
    if(read(fd, buf, 3) != 3)
        return -1;
    
    D1 = buf[0] * 65536;
	D1 += buf[1] * 256;
	D1 += buf[2];
    
	/* initiate a temperature conversion */
    cmd = CMD_D2_OSR_4096;
    if(write(fd, &cmd, 1) == -1)
        return -1;
 
 
    /* we need to wait till conversion has finished */    
    usleep(10 * 10000);
    
    cmd = CMD_READ_ADC;
    if(write(fd, &cmd, 1) == -1)
        return -1;
  
    if(read(fd, buf, 3) != 3)
        return -1;
    
    D2 = buf[0] * 65536;
	D2 += buf[1] * 256;
	D2 += buf[2];
    
    /* calculate temperature */
    dT = D2 - prom[5] * pow(2,8);
    T = (2000 + dT * (double)prom[6] / pow(2,23)) / 100;
    *temp = T;
    
    /* calculate temperature compensated pressure */
    OFF = prom[2] * pow(2,17) + dT * (double)prom[4] / pow(2,6);
    SENS = prom[1] * pow(2,16) + dT * (double)prom[3] / pow(2,7);
    P = ((D1 * SENS / pow(2,21) - OFF) / pow(2,15)) / 100;
    *pressure = P;
    
    *d_dT = dT;
    *d_D1 = D1;
    *d_D2 = D2;
    *d_OFF = OFF;
    *d_SENS = SENS;
       
    return 0;
}

/* calculate the CRC of the PROM coefficients */
uint8_t MS5607_crc4(uint16_t *n_prom) 
{ 
       uint8_t cnt;                                     
       uint16_t n_rem;                             
       uint16_t crc_read;                          
       uint8_t  n_bit; 
       n_rem = 0x00; 
       crc_read=n_prom[7];                         
       n_prom[7]=(0xFF00 & (n_prom[7]));           
       for (cnt = 0; cnt < 16; cnt++)              
       {     
            if(cnt%2 == 1) 
                n_rem ^= (uint16_t) ((n_prom[cnt >> 1]) & 0x00FF); 
            else 
                n_rem ^= (uint16_t) (n_prom[cnt >> 1] >> 8); 
            
            for(n_bit = 8; n_bit > 0; n_bit--) 
            { 
                if (n_rem & (0x8000)) 
                { 
                    n_rem = (n_rem << 1) ^ 0x3000; 
                } 
                else 
                { 
                    n_rem = (n_rem << 1); 
                } 
            } 
       } 
       n_rem = (0x000F & (n_rem >> 12)); 
       n_prom[7]=crc_read;               
       return (n_rem ^ 0x00); 
}

/* read the coefficients from PROM, they are used for software compensation */
int readPROM(int fd, uint16_t *prom, uint8_t promSize) 
{
    uint8_t i;
    uint8_t k;
    uint8_t cmd;
    uint8_t buf[2];
    uint8_t crc;
    
    /* read PROM of pressure sensor */     
    for(i = 0; i < promSize; i++)
    {
        for(k = 0; k < 1; k++)
        {
            cmd = CMD_READ_PROM + i * 2;
            if(write(fd, &cmd, 1) == -1)
                return -1;
            
            if(read(fd, buf, 2) != 2)
                return -1;
                
            prom[i] = buf[0] << 8;
            prom[i] |= buf[1];
        }
    }
    
    crc = MS5607_crc4(prom);
    if(crc != (prom[7] & 0xF))
        return -2;
    
    return 0;
}

int main (int argc,char** argv)
{
	int fd;
    uint16_t prom[8];
    int rc;
    double pressure;
    double temp;
    int iterations = 1;
    int cycles = 0;
    int humanReadable = 1;
    int debugEnabled = 0;
    uint32_t d_D1;
    uint32_t d_D2;
    int64_t d_SENS;
    int64_t d_OFF;
    int32_t d_dT;
     
    if(argc < 2)
    {
        printf("Missing parameter.\n");
        printf("Usage: %s <I2C_BUS> <ITERATIONS> <HUMAN_READABLE>\n", argv[0]);
        printf("       <ITERATIONS> is optional, must be greater than 0. Default = 1\n");
        printf("       <HUMAN_READABLE> is optional, 1...human readable output, else 0. Default = 1\n");
        printf("       <DEBUG_ENABLE> is optional, 1...debugging enabled, else 0. Default = 0\n");
        return 1;
    }
    
    I2C_BUS = argv[1];
    
    if(argc >= 3)
    {
        iterations = atoi(argv[2]);    
    }
                
    if(argc >= 4)
    {
        humanReadable = atoi(argv[3]);
    }
    
    if(argc == 5)
    {
        debugEnabled = atoi(argv[4]);
    }
        
	fd = open(I2C_BUS, O_RDWR);

	if(fd < 0)
	{
		if(humanReadable)
		    printf("opening file failed: %s\n", strerror(errno));
		return 1;
	}

	if(ioctl(fd, I2C_SLAVE, I2C_ADDR) < 0)
	{
		if(humanReadable)
		    printf("ioctl error: %s\n", strerror(errno));
		return 1;
	}
	
	rc = readPROM(fd, prom, sizeof(prom) / sizeof(uint16_t));
	if(rc == -1)
	{
	    if(humanReadable)
		    printf("Reading PROM failed.\n");
        return 1;
	}
	else if(rc == -2)
	{
	    if(humanReadable)
		    printf("PROM CRC invalid.\n");
        return 1;
	}
	
	while(argc < 2 || (argc >= 2 && cycles < iterations))
    {
        cycles++;
        
        if(readPressure(fd, prom, &pressure, &temp, &d_D1, &d_D2, &d_dT, &d_OFF, &d_SENS) != 0)
        {
            if(humanReadable)
		        printf("Measuring pressure failed.\n");
            return 1;
        }
        
        if(humanReadable)
        {
            if(debugEnabled)
		        printf("%.2f mbar, %0.2f °C, %.2f,%0.2f,%u,%u,%d,%" PRId64 ",%" PRId64 ",%u,%u,%u,%u,%u,%u,%u,%u\n", pressure, temp, pressure, temp, d_D1, d_D2, d_dT, d_OFF, d_SENS, prom[0], prom[1], prom[2], prom[3], prom[4], prom[5], prom[6], prom[7]);
		    else
		        printf("%.2f mbar, %0.2f °C\n", pressure, temp);
		}
		else
		    printf("%.2f,%0.2f\n", pressure, temp);
        sleep(1);
    }

	return 0;
}
