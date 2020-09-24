/*
 * (C) 2005-2020 MediaTek Inc. All rights reserved.
 *
 * Copyright Statement:
 *
 * This MT3620 driver software/firmware and related documentation
 * ("MediaTek Software") are protected under relevant copyright laws.
 * The information contained herein is confidential and proprietary to
 * MediaTek Inc. ("MediaTek"). You may only use, reproduce, modify, or
 * distribute (as applicable) MediaTek Software if you have agreed to and been
 * bound by this Statement and the applicable license agreement with MediaTek
 * ("License Agreement") and been granted explicit permission to do so within
 * the License Agreement ("Permitted User"). If you are not a Permitted User,
 * please cease any access or use of MediaTek Software immediately.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT MEDIATEK SOFTWARE RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
 * PROVIDED TO RECEIVER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS
 * ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH MEDIATEK SOFTWARE, AND RECEIVER AGREES TO
 * LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO MEDIATEK SOFTWARE RELEASED
 * HEREUNDER WILL BE ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
 * RECEIVER TO MEDIATEK DURING THE PRECEDING TWELVE (12) MONTHS FOR SUCH
 * MEDIATEK SOFTWARE AT ISSUE.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "printf.h"
#include "mt3620.h"
#include "os_hal_uart.h"
#include "os_hal_gpt.h"
#include "os_hal_gpio.h"
#include "os_hal_spim.h"
#include "os_hal_mbox.h"
#include "os_hal_mbox_shared_mem.h"

#include "ioLibrary_Driver/Ethernet/socket.h"
#include "ioLibrary_Driver/Ethernet/wizchip_conf.h"
#include "ioLibrary_Driver/Ethernet/W5500/w5500.h"
#include "ioLibrary_Driver/Application/loopback/loopback.h"
#include "ioLibrary_Driver/Internet/DHCP/dhcps.h"
#include "ioLibrary_Driver/Internet/SNTP/sntps.h"


/* Additional Note:
 *     A7 <--> M4 communication is handled by shared memory.
 *         mailbox fifo is used to transmit the address of the shared memory.
 *     M4 <--> M4 communication is handled by mailbox fifo.
 *         mailbox fifo is used to transmit data (4Byte CMD and 4Byte Data).
 */

/******************************************************************************/
/* Configurations */
/******************************************************************************/
/* UART */
static const uint8_t uart_port_num = OS_HAL_UART_PORT0;

uint8_t spi_master_port_num = OS_HAL_SPIM_ISU1;
uint32_t spi_master_speed = 2*10*1000; /* KHz */

extern uint32_t timestamp;

#define SPIM_CLOCK_POLARITY SPI_CPOL_0
#define SPIM_CLOCK_PHASE SPI_CPHA_0
#define SPIM_RX_MLSB SPI_MSB
#define SPIM_TX_MSLB SPI_MSB
#define SPIM_FULL_DUPLEX_MIN_LEN 1
#define SPIM_FULL_DUPLEX_MAX_LEN 16

/* Intercore Communications */
/* Maximum mailbox buffer len.
 *    Maximum message len: 1024B
 *                         1024 is the maximum value when HL_APP invoke send().
 *    Component UUID len : 16B
 *    Reserved data len  : 4B
*/
/// <summary>
///     When sending a message, this is the recipient HLApp's component ID.
///     When receiving a message, this is the sender HLApp's component ID.
/// </summary>
typedef struct {
    /// <summary>4-byte little-endian word</summary>
    uint32_t data1;
    /// <summary>2-byte little-endian half</summary>
    uint16_t data2;
    /// <summary>2-byte little-endian half</summary>
    uint16_t data3;
    /// <summary>2 bytes (big-endian) followed by 6 bytes (big-endian)</summary>
    uint8_t data4[8];
} ComponentId;

#define MBOX_BUFFER_LEN_MAX 1048
static uint8_t mbox_send_buf[MBOX_BUFFER_LEN_MAX];
static uint8_t mbox_recv_buf[MBOX_BUFFER_LEN_MAX];

// 819255ff-8640-41fd-aea7-f85d34c491d5
static const ComponentId hlAppId = { .data1 = 0x819255ff,
                                    .data2 = 0x8640,
                                    .data3 = 0x41fd,
                                    .data4 = {0xae, 0xa7, 0xf8, 0x5d, 0x34, 0xc4, 0x91, 0xd5} };

/* Bitmap for IRQ enable. bit_0 and bit_1 are used to communicate with HL_APP */
static const uint32_t mbox_irq_status = 0x3;

#define APP_STACK_SIZE_BYTES		(1024 / 4)

/****************************************************************************/
/* Global Variables */
/****************************************************************************/
struct mtk_spi_config spi_default_config = {
    .cpol = SPIM_CLOCK_POLARITY,
    .cpha = SPIM_CLOCK_PHASE,
    .rx_mlsb = SPIM_RX_MLSB,
    .tx_mlsb = SPIM_TX_MSLB,
#if 1
    // 20200527 taylor
    // W5500 NCS
    .slave_sel = SPI_SELECT_DEVICE_1,
#else
    // Original
    .slave_sel = SPI_SELECT_DEVICE_0,
#endif
};
#if 0
uint8_t spim_tx_buf[SPIM_FULL_DUPLEX_MAX_LEN];
uint8_t spim_rx_buf[SPIM_FULL_DUPLEX_MAX_LEN];
#endif
static volatile int g_async_done_flag;

// Default Static Network Configuration for TCP Server 
#if 0
wiz_NetInfo gWIZNETINFO = { {0x00, 0x08, 0xdc, 0xff, 0xfa, 0xfb},
                           {192, 168, 50, 10},
                           {255, 255, 255, 0},
                           {192, 168, 50, 1},
                           {8, 8, 8, 8},
                           NETINFO_STATIC };
#else
wiz_NetInfo gWIZNETINFO = {};
#endif

#define USE_READ_SYSRAM
#ifdef USE_READ_SYSRAM
uint8_t __attribute__((unused, section(".sysram"))) s0_Buf[2 * 1024];
uint8_t __attribute__((unused, section(".sysram"))) s1_Buf[2 * 1024];
uint8_t __attribute__((unused, section(".sysram"))) gDATABUF[DATA_BUF_SIZE];
uint8_t __attribute__((unused, section(".sysram"))) gsntpDATABUF[DATA_BUF_SIZE];
#else
uint8_t s0_Buf[2048];
uint8_t s1_Buf[2048];
uint8_t gDATABUF[DATA_BUF_SIZE];
uint8_t gsntpDATABUF[DATA_BUF_SIZE];
#endif

/* Intercore Communications */
BufferHeader *outbound, *inbound;
static uint32_t mbox_shared_buf_size;
volatile u8  blockDeqSema;
volatile u8  blockFifoSema;
static const u32 pay_load_start_offset = 20; /* UUID 16B, Reserved 4B */

/* GPIO */
static const uint8_t gpio_w5500_reset = OS_HAL_GPIO_12;
static const uint8_t gpio_w5500_ready = OS_HAL_GPIO_15;


/******************************************************************************/
/* Applicaiton Hooks */
/******************************************************************************/
/* Hook for "printf". */
void _putchar(char character)
{
	mtk_os_hal_uart_put_char(uart_port_num, character);
	if (character == '\n')
		mtk_os_hal_uart_put_char(uart_port_num, '\r');
}

/******************************************************************************/
/* Functions */
/******************************************************************************/
static int gpio_output(u8 gpio_no, u8 level)
{
	int ret;

	ret = mtk_os_hal_gpio_request(gpio_no);
	if (ret != 0) {
		printf("request gpio[%d] fail\n", gpio_no);
		return ret;
	}

	mtk_os_hal_gpio_set_direction(gpio_no, OS_HAL_GPIO_DIR_OUTPUT);
	mtk_os_hal_gpio_set_output(gpio_no, level);
	ret = mtk_os_hal_gpio_free(gpio_no);
	if (ret != 0) {
		printf("free gpio[%d] fail\n", gpio_no);
		return 0;
	}
	return 0;
}

static int gpio_input(u8 gpio_no, os_hal_gpio_data *pvalue)
{
	u8 ret;

	ret = mtk_os_hal_gpio_request(gpio_no);
	if (ret != 0) {
		printf("request gpio[%d] fail\n", gpio_no);
		return ret;
	}
	mtk_os_hal_gpio_set_direction(gpio_no, OS_HAL_GPIO_DIR_INPUT);
	mtk_os_hal_gpio_get_input(gpio_no, pvalue);
	ret = mtk_os_hal_gpio_free(gpio_no);
	if (ret != 0) {
		printf("free gpio[%d] fail\n", gpio_no);
		return ret;
	}
	return 0;
}

// check w5500 network setting
void InitPrivateNetInfo(void) {
    uint8_t tmpstr[6];
    uint8_t i = 0;
    ctlwizchip(CW_GET_ID, (void*)tmpstr);

    // if (ctlnetwork(CN_SET_NETINFO, (void*)&gWIZNETINFO) < 0) {
    //     printf("ERROR: ctlnetwork SET\r\n");
    // }
    memset((void*)&gWIZNETINFO, 0, sizeof(gWIZNETINFO));

    ctlnetwork(CN_GET_NETINFO, (void*)&gWIZNETINFO);

    printf("\r\n=== %s NET CONF ===\r\n", (char*)tmpstr);
    printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n", gWIZNETINFO.mac[0], gWIZNETINFO.mac[1], gWIZNETINFO.mac[2],
        gWIZNETINFO.mac[3], gWIZNETINFO.mac[4], gWIZNETINFO.mac[5]);

    printf("SIP: %d.%d.%d.%d\r\n", gWIZNETINFO.ip[0], gWIZNETINFO.ip[1], gWIZNETINFO.ip[2], gWIZNETINFO.ip[3]);
    printf("GAR: %d.%d.%d.%d\r\n", gWIZNETINFO.gw[0], gWIZNETINFO.gw[1], gWIZNETINFO.gw[2], gWIZNETINFO.gw[3]);
    printf("SUB: %d.%d.%d.%d\r\n", gWIZNETINFO.sn[0], gWIZNETINFO.sn[1], gWIZNETINFO.sn[2], gWIZNETINFO.sn[3]);
    printf("DNS: %d.%d.%d.%d\r\n", gWIZNETINFO.dns[0], gWIZNETINFO.dns[1], gWIZNETINFO.dns[2], gWIZNETINFO.dns[3]);
    printf("======================\r\n");

    // socket 0-7 closed
    for (i = 0; i < 8; i++)
    {
        setSn_CR(i, 0x10);
    }
    printf("Socket 0-7 Closed \r\n");
}

void w5500_init() {
    // W5500 reset
    gpio_output(gpio_w5500_reset, OS_HAL_GPIO_DATA_HIGH);

    osai_delay_ms(150);

    // W5500 ready check
    os_hal_gpio_data w5500_ready;
    gpio_input(gpio_w5500_ready, &w5500_ready);

    while (1) {
        if (w5500_ready) break;
    }
}


/* Mailbox Fifo Interrupt handler.
 * Mailbox Fifo Interrupt is triggered when mailbox fifo been R/W.
 *     data->event.channel: Channel_0 for A7, Channel_1 for the other M4.
 *     data->event.ne_sts: FIFO Non-Empty.interrupt
 *     data->event.nf_sts: FIFO Non-Full interrupt
 *     data->event.rd_int: Read FIFO interrupt
 *     data->event.wr_int: Write FIFO interrupt
*/
void mbox_fifo_cb(struct mtk_os_hal_mbox_cb_data *data)
{
	if (data->event.channel == OS_HAL_MBOX_CH0) {
		/* A7 core write data to mailbox fifo. */
		if (data->event.wr_int) {
			blockFifoSema++;
		}
	}
}

/* SW Interrupt handler.
 * SW interrupt is triggered when:
 *    1. A7 read/write the shared memory.
 *    2. The other M4 triggers SW interrupt.
 *     data->swint.swint_channel: Channel_0 for A7, Channel_1 for the other M4.
 *     Channel_0:
 *         data->swint.swint_sts bit_0: A7 read data from mailbox
 *         data->swint.swint_sts bit_1: A7 write data to mailbox
 *     Channel_1:
 *         data->swint.swint_sts bit_0: M4 sw interrupt
*/
void mbox_swint_cb(struct mtk_os_hal_mbox_cb_data *data)
{
	if (data->swint.channel == OS_HAL_MBOX_CH0) {
		if (data->swint.swint_sts & (1 << 1)) {
			blockDeqSema++;
		}
	}
}

void mbox_get_payload(u8 *mbox_buf, u32 mbox_data_len)
{
    uint8_t* timebuf;
#if 0
	u32 payload_len;
    payload_len = mbox_data_len - pay_load_start_offset;

	/* Print message as text. */
	printf("  Payload (%d bytes as text): ", payload_len);
	for (i = pay_load_start_offset; i < mbox_data_len; ++i)
		printf("%c", mbox_buf[i]);
	printf("\n");
#endif

    timebuf = &mbox_buf[pay_load_start_offset];

    SNTPs_sync_time(timebuf);
}

void mbox_init(void)
{
	struct mbox_fifo_event mask;

	/* Init buffer */
	memset(mbox_send_buf, 0, MBOX_BUFFER_LEN_MAX);

    /* Open the MBOX channel of A7 <-> M4 */
	mtk_os_hal_mbox_open_channel(OS_HAL_MBOX_CH0);

	blockDeqSema = 0;
	blockFifoSema = 0;

	/* Register interrupt callback */
	mask.channel = OS_HAL_MBOX_CH0;
	mask.ne_sts = 0;	/* FIFO Non-Empty interrupt */
	mask.nf_sts = 0;	/* FIFO Non-Full interrupt */
	mask.rd_int = 0;	/* Read FIFO interrupt */
	mask.wr_int = 1;	/* Write FIFO interrupt */
	mtk_os_hal_mbox_fifo_register_cb(OS_HAL_MBOX_CH0, mbox_fifo_cb, &mask);
	mtk_os_hal_mbox_sw_int_register_cb(OS_HAL_MBOX_CH0, mbox_swint_cb, mbox_irq_status);

	/* Get mailbox shared buffer size, defined by Azure Sphere OS. */
	if (GetIntercoreBuffers(&outbound, &inbound, &mbox_shared_buf_size) == -1) {
		printf("GetIntercoreBuffers failed\n");
		return;
	}
	printf("Mbox shared buf size = %d\n", mbox_shared_buf_size);
	// printf("Mbox local buf size = %d\n", MBOX_BUFFER_LEN_MAX);

	memcpy((void*)&mbox_send_buf, (void*)&hlAppId, sizeof(hlAppId));
}

void mbox_send_data_a7(uint8_t* sock_data, uint32_t datasize)
{
    uint32_t size;
	int result;

    memcpy((void*)&mbox_send_buf[pay_load_start_offset], sock_data, datasize);
    size = pay_load_start_offset + datasize;

    /* Write to A7, enqueue to mailbox */
	result = EnqueueData(inbound, outbound, mbox_shared_buf_size, mbox_send_buf, size);
	if (result == -1) {
		printf("Mailbox enqueue failed!\n");
	}
}

void mbox_receive_data(void)
{
    uint8_t result;
    uint32_t buf_len;

    memset(mbox_recv_buf, 0, MBOX_BUFFER_LEN_MAX);

    /* Read from A7, dequeue from mailbox */
    result = DequeueData(outbound, inbound, mbox_shared_buf_size, mbox_recv_buf, &buf_len);
    if (result == -1 || buf_len < pay_load_start_offset) {
        printf("Mailbox dequeue failed!\n");
    }
    mbox_get_payload(mbox_recv_buf, buf_len);
}

void mbox_tcp_server(uint8_t sn, uint8_t* sock_buf, uint16_t port)
{
    int32_t ret;
    uint16_t size = 0;

	for (int i = 0; i < DATA_BUF_SIZE; i++) {
        sock_buf[i] = NULL;
    }

    switch (getSn_SR(sn))
    {
    case SOCK_ESTABLISHED:
        if (getSn_IR(sn) & Sn_IR_CON)
        {
            setSn_IR(sn, Sn_IR_CON);
        }
        if ((size = getSn_RX_RSR(sn)) > 0) // Don't need to check SOCKERR_BUSY because it doesn't not occur.
        {
            if (size > DATA_BUF_SIZE)
                size = DATA_BUF_SIZE;

            ret = sock_recv(sn, sock_buf, size);

            if (ret <= 0)
                return ret; // check SOCKERR_BUSY & SOCKERR_XXX. For showing the occurrence of SOCKERR_BUSY.

            printf("Received data from socket %d : (%d) %s\r\n", sn, size, sock_buf);

            // Send data to a7 core
			mbox_send_data_a7(sock_buf, size);
        }
        break;
    case SOCK_CLOSE_WAIT:
        if ((ret = sock_disconnect(sn)) != SOCK_OK)
            return ret;
        printf("%d : Socket Closed\r\n", sn);
        break;
    case SOCK_INIT:
        printf("%d : Listen, TCP server, port [%d]\r\n", sn, port);
        if ((ret = sock_listen(sn)) != SOCK_OK)
            return ret;
        break;
    case SOCK_CLOSED:
        if ((ret = wiz_socket(sn, Sn_MR_TCP, port, 0x00)) != sn)
            return ret;
        printf("%d : Socket Opened\r\n", sn);
        break;
    default:
        break;
    }
}

_Noreturn void RTCoreMain(void)
{
    u32 i = 0;
    
    /* Init Vector Table */
    NVIC_SetupVectorTable();

    /* Init UART */
    mtk_os_hal_uart_ctlr_init(uart_port_num);
    //printf("\nUART Inited (port_num=%d)\n", uart_port_num);

    /* Init SPIM */
    mtk_os_hal_spim_ctlr_init(spi_master_port_num);

    printf("--------------------------------\r\n");
    printf("W5500_RTApp_MT3620_BareMetal\r\n");
    printf("App built on: " __DATE__ " " __TIME__ "\r\n");

    /* Init W5500 */
    w5500_init();

    InitPrivateNetInfo();
// #define TEST_AX1
    dhcps_init(2, gDATABUF);
#ifndef TEST_AX1
    SNTPs_init(3, gsntpDATABUF);
#endif

#if 1
    printf("s0_Buf = %#x\r\n", s0_Buf);
    printf("s1_Buf = %#x\r\n", s1_Buf);
    printf("gDATABUF = %#x\r\n", gDATABUF);
    printf("gsntpDATABUF = %#x\r\n", gsntpDATABUF);
#endif

	mbox_init();

#if 0
	for (;;) 
		__asm__("wfi");
#else
    while (1)
    {
        dhcps_run();
        
#ifndef TEST_AX1
        SNTPs_run();
#endif
        // loopback_tcps(0, s0_Buf, 50000);
        loopback_tcps(1, s1_Buf, 50001);

		mbox_tcp_server(0, s0_Buf, 5000);
        if (blockDeqSema != 0) {
            mbox_receive_data();
            blockDeqSema--;
        }

#ifndef TEST_AX1
        i++;
        if(i > 10000)
        {
          timestamp++;
          i = 0;
        }
#endif
    }
#endif

}

