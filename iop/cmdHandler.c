/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * Copyright (C) 2003,2004 adresd (adresd_ps2dev@yahoo.com)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <types.h>
#include <ioman.h>
#include <cdvdman.h>
#include <sysclib.h>
#include <stdio.h>
#include <thbase.h>
#include <intrman.h>
#include <sifman.h>
#include <sifrpc.h>
#include <modload.h>
#include <ps2lib_err.h>
#include <poweroff.h>
#include <sifcmd.h>

#include "ps2ip.h"
#include "hostlink.h"
#include "net_fsys.h"
#include "globals.h"

#define BUF_SIZE 1024
static char recvbuf[BUF_SIZE] __attribute__((aligned(16)));
static unsigned int rpc_data[1024 / 4] __attribute__((aligned(16)));

int excepscrdump = 1;

#define PKO_DMA_DEST ((void *)0x200ff800)
//unsigned int *dma_ptr =(unsigned int*)(0x20100000-2048);

//////////////////////////////////////////////////////////////////////////
static void
pkoExecIop(char *buf, int len)
{
    pko_pkt_execiop_req *cmd;
    int retval;
    int arglen;
    char *path;
    char *args;
    unsigned int argc;
    int id;
    int i;

    cmd = (pko_pkt_execiop_req *)buf;

    dbgprintf("IOP cmd: EXECIOP\n");

    if (len != sizeof(pko_pkt_execiop_req)) {
        dbgprintf("IOP cmd: exec_iop got a broken packet (%d)!\n", len);
        return;
    }

    // Make sure arg vector is null-terminated
    cmd->argv[PKO_MAX_PATH - 1] = '\0';

    printf("IOP cmd: %ld args\n", ntohl(cmd->argc));

    path = &cmd->argv[0];
    args = &cmd->argv[strlen(cmd->argv) + 1];
    argc = ntohl(cmd->argc);

    printf("IOP binary path: %s\n", path);

    arglen = 0;
    for (i = 0; i < (argc - 1); i++) {
        printf("arg %d: %s (%d)\n", i, &args[arglen], arglen);
        arglen += strlen(&args[arglen]) + 1;
    }

    id = LoadStartModule(cmd->argv, arglen, args, &retval);

    if (id < 0) {
        printf("Error loading module: ");
        switch (-id) {
            case E_IOP_INTR_CONTEXT:
                printf("IOP is in exception context.\n");
                break;
            case E_IOP_DEPENDANCY:
                printf("Inter IRX dependancy error.\n");
                break;
            case E_LF_NOT_IRX:
                printf("Invalid IRX module.\n");
                break;
            case E_LF_FILE_NOT_FOUND:
                printf("Unable to open executable file.\n");
                break;
            case E_LF_FILE_IO_ERROR:
                printf("Error while accessing file.\n");
                break;
            case E_IOP_NO_MEMORY:
                printf("IOP is out of memory.\n");
                break;
            default:
                printf("Unknow error code: %d\n", -retval);
                break;
        }
    } else {
        printf("loadmodule: id %d, ret %d\n", id, retval);
    }
}

//////////////////////////////////////////////////////////////////////////
unsigned int
pkoSetSifDma(void *dest, void *src, unsigned int length, unsigned int mode)
{
    struct t_SifDmaTransfer sendData;
    int oldIrq;
    int id;

    sendData.src = (unsigned int *)src;
    sendData.dest = (unsigned int *)dest;
    sendData.size = length;
    sendData.attr = mode;

    CpuSuspendIntr(&oldIrq);
    id = sceSifSetDma(&sendData, 1);
    CpuResumeIntr(oldIrq);

    return id;
}

//////////////////////////////////////////////////////////////////////////
unsigned int
pkoSendSifCmd(unsigned int cmd, void *src, unsigned int len)
{
    unsigned int dmaId;
    unsigned int dmaLen;

    rpc_data[0] = cmd;

    memcpy(&rpc_data[1], src,
           (len > sizeof(rpc_data) ? sizeof(rpc_data) : len));

    len = len > sizeof(rpc_data) ? sizeof(rpc_data) : len;

    /* Round DMA size up to 16-byte boundary — real HW SIF DMA
       requires aligned transfer sizes.  rpc_data is 1KB so the
       extra bytes are just trailing zeros. */
    dmaLen = (len + 15) & ~15;

    dmaId = pkoSetSifDma(PKO_DMA_DEST, rpc_data, dmaLen, 4);

    if (dmaId == 0) {
        printf("IOP: sifSendCmd %x failed\n", cmd);
        return -1;
    }
    return 0;
}


//////////////////////////////////////////////////////////////////////////
static void
pkoExecEE(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_EXECEE, buf, len);
};
//////////////////////////////////////////////////////////////////////////
static void
pkoGSExec(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_GSEXEC, buf, len);
};
//////////////////////////////////////////////////////////////////////////
static void
pkoNetDump(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_NETDUMP, buf, len);
};
//////////////////////////////////////////////////////////////////////////
static void
pkoScrDump(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_SCRDUMP, buf, len);
};

//////////////////////////////////////////////////////////////////////////
static void
pkoPowerOff()
{
    PoweroffShutdown();
}

//////////////////////////////////////////////////////////////////////////
static void pkoStatusStarting(void);

static void
pkoReset(char *buf, int len)
{
    dbgprintf("IOP cmd: RESET\n");

    if (len != sizeof(pko_pkt_reset_req)) {
        dbgprintf("IOP cmd: exec_ee got a broken packet (%d)!\n", len);
        return;
    }

    pkoStatusStarting();
    printf("unmounting\n");
    fsysUnmount();
    printf("unmounted\n");
    DelDrv("tty");

    pkoSendSifCmd(PKO_RPC_RESET, buf, len);
};

static void
pkoStopVU(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_STOPVU, buf, len);
};

static void
pkoStartVU(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_STARTVU, buf, len);
};

static void
pkoDumpMem(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_DUMPMEM, buf, len);
};

static void
pkoDumpReg(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_DUMPREG, buf, len);
};

static void
pkoWriteMem(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_WRITEMEM, buf, len);
};

//////////////////////////////////////////////////////////////////////////
// The status record: set by the EE (mxlink, NPM_STATUS) when a program
// starts and when it is ready, and answered from here to PKO_STATUS_CMD:
// no EE, no host: connection, so a host may ask at any time.
static struct
{
    unsigned int state;
    unsigned int returns;
    int last_status;
    char path[PKO_MAX_PATH];
} status;

// A reset: not ready from here on, the EE may not be told in time.
static void pkoStatusStarting(void)
{
    status.state = 0;
}

void pkoSetStatus(const void *data, int size)
{
    if (size > sizeof(status))
        size = sizeof(status);
    memcpy(&status, data, size);
    status.path[PKO_MAX_PATH - 1] = '\0';
}

// What this PS2 is: read once, before a program can use the CD/DVD controller.
static char model[16] = "unknown";
static char rom[16] = "unknown";

// The model, by the controller's command 0x17 in two halves, as sceCdRM. Not
// sceCdRM itself: rom0:CDVDMAN does not export it (only XCDVDMAN does), and
// an import it lacks would stop this module loading. Below controller version
// 0x10500 it is not asked, as sceCdRM does not ask either. The name is what
// the PS2 has stored, not a hardware identity.
static void readModel(void)
{
    static const char mv = 0, halves[2] = {0, 8};
    unsigned char out[16];
    char name[16];
    int i, n;

    if (sceCdApplySCmd(0x03, &mv, 1, out) != 1 ||
        (out[3] | out[2] << 8 | out[1] << 16) < 0x10500)
        return;
    for (i = 0; i < 2; i++) {
        if (sceCdApplySCmd(0x17, &halves[i], 1, out) != 1 || out[0] != 0)
            return;
        memcpy(&name[i * 8], &out[1], 8);
    }
    for (n = 0; n < 15 && name[n] > ' ' && name[n] < 0x7f; n++)
        ;
    if (n == 0)
        return;
    memcpy(model, name, n);
    model[n] = '\0';
}

static void readRom(void)
{
    char v[14];
    int fd = open("rom0:ROMVER", FIO_O_RDONLY);

    if (fd < 0)
        return;
    if (read(fd, v, sizeof(v)) == sizeof(v)) {
        memcpy(rom, v, sizeof(v));
        rom[sizeof(v)] = '\0';
    }
    close(fd);
}

void pkoReadIdentity(void)
{
    readModel();
    readRom();
}

static void
pkoStatus(int sock, struct sockaddr_in *to, int tolen)
{
    static pko_pkt_status_rly rly;

    rly.cmd = htonl(PKO_STATUS_RLY);
    rly.len = htons(sizeof(rly));
    rly.version = htonl(2);
    rly.state = htonl(status.state);
    rly.returns = htonl(status.returns);
    rly.last_status = htonl(status.last_status);
    memcpy(rly.path, status.path, PKO_MAX_PATH);
    memcpy(rly.model, model, sizeof(rly.model));
    memcpy(rly.rom, rom, sizeof(rly.rom));
    sendto(sock, &rly, sizeof(rly), 0, (struct sockaddr *)to, tolen);
}

//////////////////////////////////////////////////////////////////////////
static void
cmdListener(int sock)
{
    while (1) {
        struct sockaddr_in remote_addr;
        int len;
        int addrlen;
        pko_pkt_hdr *header;
        unsigned int cmd;

        addrlen = sizeof(remote_addr);
        len = recvfrom(sock, &recvbuf[0], BUF_SIZE, 0,
                       (struct sockaddr *)&remote_addr,
                       &addrlen);
        dbgprintf("IOP cmd: received packet (%d)\n", len);

        if (len < 0) {
            dbgprintf("IOP: cmdListener: recvfrom error (%d)\n", len);
            continue;
        }
        if (len < sizeof(pko_pkt_hdr)) {
            continue;
        }

        header = (pko_pkt_hdr *)recvbuf;
        cmd = ntohl(header->cmd);
        switch (cmd) {

            case PKO_EXECIOP_CMD:
                pkoExecIop(recvbuf, len);
                break;
            case PKO_EXECEE_CMD:
                pkoExecEE(recvbuf, len);
                break;
            case PKO_POWEROFF_CMD:
                pkoPowerOff();
                break;
            case PKO_RESET_CMD:
                pkoReset(recvbuf, len);
                break;
            case PKO_SCRDUMP_CMD:
                excepscrdump = 1;
                pkoScrDump(recvbuf, len);
                break;
            case PKO_NETDUMP_CMD:
                excepscrdump = 0;
                pkoNetDump(recvbuf, len);
                break;
            case PKO_START_VU:
                pkoStartVU(recvbuf, len);
                break;
            case PKO_STOP_VU:
                pkoStopVU(recvbuf, len);
                break;
            case PKO_DUMP_MEM:
                pkoDumpMem(recvbuf, len);
                break;
            case PKO_DUMP_REG:
                pkoDumpReg(recvbuf, len);
                break;
            case PKO_GSEXEC_CMD:
                pkoGSExec(recvbuf, len);
                break;
            case PKO_WRITE_MEM:
                pkoWriteMem(recvbuf, len);
                break;
            case PKO_STATUS_CMD:
                pkoStatus(sock, &remote_addr, addrlen);
                break;
            default:
                dbgprintf("IOP cmd: Uknown cmd received\n");
                break;
        }
        dbgprintf("IOP cmd: waiting for next pkt\n");
    }
}

static void
cmdPowerOff(void *arg)
{
#ifdef PWOFFONRESET
    pkoPowerOff();
#else
    pko_pkt_reset_req reset;

    reset.cmd = htonl(PKO_RESET_CMD);
    reset.len = 0;

    pkoReset((char *)&reset, sizeof(reset));
#endif
}

//////////////////////////////////////////////////////////////////////////
static void
cmdThread(void *arg)
{
    struct sockaddr_in serv_addr;
    //    struct sockaddr_in remote_addr;
    int sock;
    int ret;

    dbgprintf("IOP cmd: Server Thread Started.\n");

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        dbgprintf("IOP cmd: Socket error %d\n", sock);
        ExitDeleteThread();
    }

    memset((void *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PKO_CMD_PORT);

    ret = bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret < 0) {
        dbgprintf("IOP cmd: Udp bind error (%d)\n", sock);
        ExitDeleteThread();
    }

    // Do tha thing
    dbgprintf("IOP cmd: Listening\n");

    cmdListener(sock);

    ExitDeleteThread();
}


//////////////////////////////////////////////////////////////////////////
int cmdHandlerInit(void)
{
    iop_thread_t thread;
    int pid;

    dbgprintf("IOP cmd: Starting thread\n");

    SifInitRpc(0);
    SetPowerButtonHandler(cmdPowerOff, NULL);

    thread.attr = 0x02000000;
    thread.option = 0;
    thread.thread = (void *)cmdThread;
    thread.stacksize = 0x800;
    thread.priority = 60; //0x1e;

    pid = CreateThread(&thread);
    if (pid >= 0) {
        int ret;
        ret = StartThread(pid, 0);
        if (ret < 0) {
            dbgprintf("IOP cmd: Could not start thread\n");
        }
    } else {
        dbgprintf("IOP cmd: Could not create thread\n");
    }
    return 0;
}
