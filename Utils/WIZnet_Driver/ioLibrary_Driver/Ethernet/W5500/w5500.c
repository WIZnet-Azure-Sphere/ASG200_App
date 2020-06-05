//*****************************************************************************
//
//! \file w5500.c
//! \brief W5500 HAL Interface.
//! \version 1.0.2
//! \date 2013/10/21
//! \par  Revision history
//!       <2015/02/05> Notice
//!        The version history is not updated after this point.
//!        Download the latest version directly from GitHub. Please visit the our GitHub repository for ioLibrary.
//!        >> https://github.com/Wiznet/ioLibrary_Driver
//!       <2014/05/01> V1.0.2
//!         1. Implicit type casting -> Explicit type casting. Refer to M20140501
//!            Fixed the problem on porting into under 32bit MCU
//!            Issued by Mathias ClauBen, wizwiki forum ID Think01 and bobh
//!            Thank for your interesting and serious advices.
//!       <2013/12/20> V1.0.1
//!         1. Remove warning
//!         2. WIZCHIP_READ_BUF WIZCHIP_WRITE_BUF in case _WIZCHIP_IO_MODE_SPI_FDM_
//!            for loop optimized(removed). refer to M20131220
//!       <2013/10/21> 1st Release
//! \author MidnightCow
//! \copyright
//!
//! Copyright (c)  2013, WIZnet Co., LTD.
//! All rights reserved.
//!
//! Redistribution and use in source and binary forms, with or without
//! modification, are permitted provided that the following conditions
//! are met:
//!
//!     * Redistributions of source code must retain the above copyright
//! notice, this list of conditions and the following disclaimer.
//!     * Redistributions in binary form must reproduce the above copyright
//! notice, this list of conditions and the following disclaimer in the
//! documentation and/or other materials provided with the distribution.
//!     * Neither the name of the <ORGANIZATION> nor the names of its
//! contributors may be used to endorse or promote products derived
//! from this software without specific prior written permission.
//!
//! THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//! AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//! IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//! ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
//! LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//! CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//! SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//! INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//! CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//! ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
//! THE POSSIBILITY OF SUCH DAMAGE.
//
//*****************************************************************************
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "w5500.h"
#include "w5500-dbg.h"
#include "printf.h"

#include "os_hal_spim.h"

#define _W5500_SPI_VDM_OP_ 0x00
#define _W5500_SPI_FDM_OP_LEN1_ 0x01
#define _W5500_SPI_FDM_OP_LEN2_ 0x02
#define _W5500_SPI_FDM_OP_LEN4_ 0x03

#if (_WIZCHIP_ == 5500)
////////////////////////////////////////////////////

#define USE_VDM
//#define USE_READ_DMA
//#define USE_WRITE_DMA

extern uint8_t spi_master_port_num;
extern uint32_t spi_master_speed;
extern struct mtk_spi_config spi_default_config;

uint8_t WIZCHIP_READ(uint32_t AddrSel)
{
    struct mtk_spi_transfer xfer;
    int ret;

    uint8_t rb;

    #ifdef USE_VDM
    AddrSel |= (_W5500_SPI_READ_ | _W5500_SPI_VDM_OP_);
    #else
    AddrSel |= (_W5500_SPI_READ_ | _W5500_SPI_FDM_OP_LEN1_);
    #endif
    
    xfer.tx_buf = NULL;
    xfer.rx_buf = &rb;
    xfer.use_dma = 0;
    xfer.speed_khz = spi_master_speed;
    xfer.len = 1;
    xfer.opcode = 0x5a;
    xfer.opcode_len = 3;

    uint8_t data[] = { (AddrSel & 0x00FF0000) >> 16, (AddrSel & 0x0000FF00) >> 8, (AddrSel & 0x000000FF) >> 0 };

    xfer.opcode = (u32)(data[2] | data[1] << 8 | data[0] << 16) & 0xffffff;
    xfer.opcode_len = 3;

    ret = mtk_os_hal_spim_transfer((spim_num)spi_master_port_num,
        &spi_default_config, &xfer);
    if (ret) {
        printf("mtk_os_hal_spim_transfer failed\n");
        return ret;
    }

    return rb;
}

void WIZCHIP_WRITE(uint32_t AddrSel, uint8_t wb)
{
    struct mtk_spi_transfer xfer;
    int ret;

    #ifdef USE_VDM
    AddrSel |= (_W5500_SPI_WRITE_ | _W5500_SPI_VDM_OP_);
    #else
    AddrSel |= (_W5500_SPI_WRITE_ | _W5500_SPI_FDM_OP_LEN1_);
    #endif

    xfer.tx_buf = &wb;
    xfer.rx_buf = NULL;
    xfer.use_dma = 0;
    xfer.speed_khz = spi_master_speed;
    xfer.len = 1;
    xfer.opcode = 0x5a;
    xfer.opcode_len = 3;

    const uint8_t data[] = { (AddrSel & 0x00FF0000) >> 16, (AddrSel & 0x0000FF00) >> 8, (AddrSel & 0x000000FF) >> 0, wb };

    xfer.opcode = (u32)(data[2] | data[1] << 8 | data[0] << 16) & 0xffffff;
    xfer.opcode_len = 3;

    ret = mtk_os_hal_spim_transfer((spim_num)spi_master_port_num,
        &spi_default_config, &xfer);
    if (ret) {
        printf("mtk_os_hal_spim_transfer failed\n");
        return ret;
    }
}

#ifdef USE_READ_DMA
void WIZCHIP_READ_BUF_DMA(uint32_t AddrSel, uint8_t* pBuf, uint16_t len)
{
    struct mtk_spi_transfer xfer;
    int ret;
    uint8_t data[3];
    uint8_t temp[4];
    uint16_t addr;
    uint32_t sent_byte = 0;

#ifdef USE_VDM
    AddrSel |= (_W5500_SPI_READ_ | _W5500_SPI_VDM_OP_);
#else
    AddrSel |= (_W5500_SPI_READ_ | _W5500_SPI_FDM_OP_LEN1_);
#endif

    memset(&xfer, 0, sizeof(xfer));

    xfer.tx_buf = NULL;
    xfer.rx_buf = pBuf;
    xfer.use_dma = 1;
    xfer.speed_khz = spi_master_speed;
    xfer.len = len;
    xfer.opcode = 0x5a;
    xfer.opcode_len = 3;

#define RBUF_SIZE_TEST 16

    do
    {
        addr = ((AddrSel >> 8) + sent_byte) & 0xFFFF;
        data[0] = (addr >> 8) & 0xff;
        data[1] = (addr >> 0) & 0xff;
        data[2] = AddrSel & 0xff;

        xfer.opcode = (u32)(data[2] | data[1] << 8 | data[0] << 16) & 0xffffff;
#ifdef DEBUG_WIZCHIP_READ_BUF
        printf("xfer.opcode = #%x\r\n", (u32)(data[2] | data[1] << 8 | data[0] << 16) & 0xffffff);
        printf("xfer.opcode = #%x\r\n", xfer.opcode);
#endif
        xfer.opcode_len = 3;

#ifdef DEBUG_WIZCHIP_READ_BUF
        printf("len = %d\r\n", len);
#endif
        if (len >= RBUF_SIZE_TEST)
        {
            xfer.len = RBUF_SIZE_TEST;
            len -= RBUF_SIZE_TEST;
        }
        else
        {
            xfer.len = len;
            len = 0;
        }

        xfer.rx_buf = pBuf + sent_byte;

        ret = mtk_os_hal_spim_transfer((spim_num)spi_master_port_num,
            &spi_default_config, &xfer);
        if (ret) {
            printf("mtk_os_hal_spim_transfer failed\n");
            return ret;
        }
        
        sent_byte += RBUF_SIZE_TEST;
    } while (len != 0);

#ifdef DEBUG_WIZCHIP_READ_BUF
    for (int i = 0; i < 3; i++)
    {
        printf("op[%d] %#x ", i, data[i]);
    }
    for (int i = 0; i < len; i++)
    {
        printf("%#x ", *(pBuf + i));
    }
    printf("\r\n");
#endif
}
#endif

void WIZCHIP_READ_BUF(uint32_t AddrSel, uint8_t* pBuf, uint16_t len)
{
    struct mtk_spi_transfer xfer;
    int ret;
    uint8_t data[3];
    uint8_t temp[4];
    uint16_t addr;
    uint32_t sent_byte = 0;

#ifdef USE_VDM
    AddrSel |= (_W5500_SPI_READ_ | _W5500_SPI_VDM_OP_);
#else
    AddrSel |= (_W5500_SPI_READ_ | _W5500_SPI_FDM_OP_LEN1_);
#endif

    memset(&xfer, 0, sizeof(xfer));

    xfer.tx_buf = NULL;
    xfer.rx_buf = pBuf;
    xfer.use_dma = 0;
    xfer.speed_khz = spi_master_speed;
    xfer.len = len;
    xfer.opcode = 0x5a;
    xfer.opcode_len = 3;

#define RBUF_SIZE_TEST 16

    do
    {
        addr = ((AddrSel >> 8) + sent_byte) & 0xFFFF;
        data[0] = (addr >> 8) & 0xff;
        data[1] = (addr >> 0) & 0xff;
        data[2] = AddrSel & 0xff;

        xfer.opcode = (u32)(data[2] | data[1] << 8 | data[0] << 16) & 0xffffff;
#ifdef DEBUG_WIZCHIP_READ_BUF
        printf("xfer.opcode = #%x\r\n", (u32)(data[2] | data[1] << 8 | data[0] << 16) & 0xffffff);
        printf("xfer.opcode = #%x\r\n", xfer.opcode);
#endif
        xfer.opcode_len = 3;

#ifdef DEBUG_WIZCHIP_READ_BUF
        printf("len = %d\r\n", len);
#endif
        if (len >= RBUF_SIZE_TEST)
        {
            xfer.len = RBUF_SIZE_TEST;
            len -= RBUF_SIZE_TEST;
        }
        else
        {
            xfer.len = len;
            len = 0;
        }

        xfer.rx_buf = pBuf + sent_byte;

        ret = mtk_os_hal_spim_transfer((spim_num)spi_master_port_num,
            &spi_default_config, &xfer);
        if (ret) {
            printf("mtk_os_hal_spim_transfer failed\n");
            return ret;
        }
        
        sent_byte += RBUF_SIZE_TEST;
    } while (len != 0);

#ifdef DEBUG_WIZCHIP_READ_BUF
    for (int i = 0; i < 3; i++)
    {
        printf("op[%d] %#x ", i, data[i]);
    }
    for (int i = 0; i < len; i++)
    {
        printf("%#x ", *(pBuf + i));
    }
    printf("\r\n");
#endif
}

void WIZCHIP_WRITE_BUF(uint32_t AddrSel, uint8_t *pBuf, uint16_t len)
{
    struct mtk_spi_transfer xfer;
    int ret;
    uint8_t data[3];
    uint8_t temp[4];
    uint16_t addr;
    uint32_t sent_byte = 0;

#ifdef USE_VDM
    AddrSel |= (_W5500_SPI_WRITE_ | _W5500_SPI_VDM_OP_);
#else
    AddrSel |= (_W5500_SPI_WRITE_ | _W5500_SPI_FDM_OP_LEN1_);
#endif

    memset(&xfer, 0, sizeof(xfer));

    xfer.tx_buf = pBuf;
    xfer.rx_buf = NULL;
#ifdef USE_WRITE_DMA
    xfer.use_dma = 1;
#else
    xfer.use_dma = 0;
#endif
    xfer.speed_khz = spi_master_speed;
    xfer.len = len;
    xfer.opcode = 0x5a;
    xfer.opcode_len = 3;

#define WBUF_SIZE_TEST 16

    do
    {
        addr = ((AddrSel >> 8) + sent_byte) & 0xFFFF;
        data[0] = (addr >> 8) & 0xff;
        data[1] = (addr >> 0) & 0xff;
        data[2] = AddrSel & 0xff;

        xfer.opcode = (u32)(data[2] | data[1] << 8 | data[0] << 16) & 0xffffff;
#ifdef DEBUG_WIZCHIP_WRITE_BUF
        printf("xfer.opcode = #%x\r\n", (u32)(data[2] | data[1] << 8 | data[0] << 16) & 0xffffff);
        printf("xfer.opcode = #%x\r\n", xfer.opcode);
#endif
        xfer.opcode_len = 3;

#ifdef DEBUG_WIZCHIP_WRITE_BUF
        printf("len = %d\r\n", len);
#endif
        if (len >= WBUF_SIZE_TEST)
        {
            xfer.len = WBUF_SIZE_TEST;
            len -= WBUF_SIZE_TEST;
        }
        else
        {
            xfer.len = len;
            len = 0;
        }

        xfer.tx_buf = pBuf + sent_byte;

        ret = mtk_os_hal_spim_transfer((spim_num)spi_master_port_num,
            &spi_default_config, &xfer);
        if (ret) {
            printf("mtk_os_hal_spim_transfer failed\n");
            return ret;
        }

        sent_byte += WBUF_SIZE_TEST;

    } while (len != 0);

#ifdef DEBUG_WIZCHIP_WRITE_BUF
    for (int i = 0; i < 3; i++)
    {
        printf("op[%d] %#x ", i, data[i]);
    }
    for (int i = 0; i < len; i++)
    {
        printf("%#x ", *(pBuf+i));
    }
    printf("\r\n");
#endif
}


uint16_t getSn_TX_FSR(uint8_t sn)
{
    uint16_t val = 0, val1 = 0;

    do
    {
        val1 = WIZCHIP_READ(Sn_TX_FSR(sn));
        val1 = (val1 << 8) + WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_TX_FSR(sn), 1));
        if (val1 != 0)
        {
            val = WIZCHIP_READ(Sn_TX_FSR(sn));
            val = (val << 8) + WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_TX_FSR(sn), 1));
        }
    } while (val != val1);
    return val;
}


uint16_t getSn_RX_RSR(uint8_t sn)
{
    uint16_t val = 0, val1 = 0;

    do
    {
        val1 = WIZCHIP_READ(Sn_RX_RSR(sn));
        val1 = (val1 << 8) + WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_RX_RSR(sn), 1));
        if (val1 != 0)
        {
            val = WIZCHIP_READ(Sn_RX_RSR(sn));
            val = (val << 8) + WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_RX_RSR(sn), 1));
        }
    } while (val != val1);
    return val;
}


void wiz_send_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
    uint16_t ptr = 0;
    uint32_t addrsel = 0;

    if (len == 0)
        return;
    ptr = getSn_TX_WR(sn);

    //M20140501 : implict type casting -> explict type casting
    //addrsel = (ptr << 8) + (WIZCHIP_TXBUF_BLOCK(sn) << 3);
    addrsel = ((uint32_t)ptr << 8) + (WIZCHIP_TXBUF_BLOCK(sn) << 3);
    //
    
    WIZCHIP_WRITE_BUF(addrsel, wizdata, len);

    ptr += len;
    setSn_TX_WR(sn, ptr);
}


void wiz_recv_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
    uint16_t ptr = 0;
    uint32_t addrsel = 0;

    if (len == 0)
        return;
    ptr = getSn_RX_RD(sn);
    //M20140501 : implict type casting -> explict type casting
    //addrsel = ((ptr << 8) + (WIZCHIP_RXBUF_BLOCK(sn) << 3);
    addrsel = ((uint32_t)ptr << 8) + (WIZCHIP_RXBUF_BLOCK(sn) << 3);
    //

    #ifdef USE_READ_DMA
    WIZCHIP_READ_BUF_DMA(addrsel, wizdata, len);    
    #else
    WIZCHIP_READ_BUF(addrsel, wizdata, len);
    #endif

    ptr += len;

    setSn_RX_RD(sn, ptr);
}

void wiz_recv_ignore(uint8_t sn, uint16_t len)
{
    uint16_t ptr = 0;

    ptr = getSn_RX_RD(sn);
    ptr += len;
    setSn_RX_RD(sn, ptr);
}

#endif
