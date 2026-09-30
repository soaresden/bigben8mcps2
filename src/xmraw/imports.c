#include <irx.h>
#include <sysclib.h>
#include <sio2man.h>
#include <thbase.h>
#include <sifcmd.h>

DECLARE_IMPORT_TABLE(sysclib, 1, 1)
I_memcpy
I_memset
END_IMPORT_TABLE

sio2man_IMPORTS_start
I_sio2_mc_transfer_init
I_sio2_transfer
sio2man_IMPORTS_end

DECLARE_IMPORT_TABLE(thbase, 1, 1)
I_CreateThread
I_StartThread
I_GetThreadId
END_IMPORT_TABLE

DECLARE_IMPORT_TABLE(sifcmd, 1, 1)
I_sceSifInitRpc
I_sceSifRegisterRpc
I_sceSifSetRpcQueue
I_sceSifRpcLoop
END_IMPORT_TABLE
