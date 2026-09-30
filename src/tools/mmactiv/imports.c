#include <irx.h>
#include <sysclib.h>
#include <sio2man.h>
#include <secrman.h>
#include <thbase.h>

DECLARE_IMPORT_TABLE(sysclib, 1, 1)
I_memcpy
I_memset
END_IMPORT_TABLE

sio2man_IMPORTS_start
I_sio2_mc_transfer_init
I_sio2_transfer
sio2man_IMPORTS_end

DECLARE_IMPORT_TABLE(secrman, 1, 3)
I_SecrSetMcCommandHandler
I_SecrSetMcDevIDHandler
I_SecrAuthCard
END_IMPORT_TABLE

thbase_IMPORTS_start
I_DelayThread
thbase_IMPORTS_end
