#include <ntifs.h>
#include <intrin.h>

int _fltused = 0; // kernel driver: disable floating-point requirement

#define DEV L"\\Device\\P2S"
#define SYM L"\\DosDevices\\P2S"
#define CTL(dev,func,meth,acc) ((dev)<<16|(acc)<<14|(func)<<2|(meth))
#define IOCTL(c,m) CTL(0x8320,c,m,3)
#define MAP  IOCTL(0x811,3)
#define UNM  IOCTL(0x812,3)
#define CHN  IOCTL(0x816,0)
#define SCN  IOCTL(0x817,0)
#define MOD  IOCTL(0x818,0)
#define MAXM 256
#define FLW 4

typedef struct { ULONG pid; ULONG f; ULONGLONG va; SIZE_T sz; } MREQ;
typedef struct { ULONGLONG uva; SIZE_T msz; ULONGLONG pa; LONG st; } MRSP;
typedef struct { ULONG pid; ULONG f; ULONGLONG mod; ULONG d; ULONGLONG o[16]; } CREQ;
typedef struct { ULONGLONG fin; LONG st; ULONGLONG l[16]; } CRSP;
typedef struct { UCHAR v; UCHAR m; } PT;
typedef struct { ULONG pid; ULONG f; ULONGLONG sa; ULONGLONG ea; ULONG pl; ULONG _; PT pat[64]; ULONG mx; ULONG _2; ULONGLONG ex; } SREQ;
typedef struct { ULONGLONG a; ULONG _; } SR;
typedef struct { ULONG cnt; SR r[256]; } SRSP;
typedef struct { ULONG pid; WCHAR n[64]; } MODR;
typedef struct { ULONGLONG base; ULONGLONG sz; LONG st; } MODS;
typedef struct { ULONGLONG uva; ULONGLONG tva; ULONGLONG pa; SIZE_T sz; ULONG pid; ULONG f; PVOID mdl; PEPROCESS tp; } ME;

static ME gm[MAXM]; static FAST_MUTEX lk;

static VOID fr(PEPROCESS p, PVOID va, SIZE_T sz) {
    __try { PMDL m = IoAllocateMdl(va,(ULONG)sz,FALSE,FALSE,NULL); if(!m)return;
        __try{MmProbeAndLockPages(m,KernelMode,IoModifyAccess);MmUnlockPages(m);}__except(1){}
        IoFreeMdl(m); }__except(1){}
}

static ULONGLONG v2p(PEPROCESS p, PVOID va) {
    ULONGLONG pa=0; KAPC_STATE a;
    KeStackAttachProcess(p,&a);
    __try{pa=MmGetPhysicalAddress(va).QuadPart;}__except(1){pa=0;}
    KeUnstackDetachProcess(&a);
    return pa;
}

static ME* ffs() { for(int i=0;i<MAXM;i++)if(!gm[i].mdl)return&gm[i]; return NULL; }
static ME* fva(ULONGLONG v) { for(int i=0;i<MAXM;i++)if(gm[i].uva==v&&gm[i].mdl)return&gm[i]; return NULL; }

static void fe(ME* e) {
    if(!e||!e->mdl)return;
    if(e->uva)__try{MmUnmapLockedPages((PVOID)(ULONG_PTR)e->uva,(PMDL)e->mdl);}__except(1){}
    MmUnlockPages((PMDL)e->mdl);IoFreeMdl((PMDL)e->mdl);
    ObDereferenceObject(e->tp);
    RtlZeroMemory(e,sizeof(*e));
}

static NTSTATUS m1(PEPROCESS tp, ULONGLONG va, SIZE_T sz, ULONG f, MRSP* rs, ME** os) {
    PVOID uva=NULL; PMDL m=NULL;
    sz=(sz+0xFFF)&~0xFFF; fr(tp,(PVOID)va,sz);
    m=IoAllocateMdl((PVOID)va,(ULONG)sz,FALSE,FALSE,NULL);
    if(!m)return STATUS_INSUFFICIENT_RESOURCES;
    __try{MmProbeAndLockPages(m,KernelMode,f&FLW?IoModifyAccess:IoReadAccess);}__except(1){IoFreeMdl(m);return GetExceptionCode();}
    MEMORY_CACHING_TYPE cc=MmCached;
    if(f&1)cc=MmNonCached;else if(f&2)cc=MmWriteCombined;
    __try{uva=MmMapLockedPagesSpecifyCache(m,UserMode,cc,NULL,FALSE,NormalPagePriority);}__except(1){MmUnlockPages(m);IoFreeMdl(m);return GetExceptionCode();}
    if(!uva){MmUnlockPages(m);IoFreeMdl(m);return STATUS_INSUFFICIENT_RESOURCES;}
    ULONGLONG pa=v2p(tp,(PVOID)va);
    if(rs){rs->uva=(ULONG_PTR)uva;rs->pa=pa;rs->msz=sz;rs->st=STATUS_SUCCESS;}
    ExAcquireFastMutex(&lk);ME* sl=ffs();
    if(!sl){ExReleaseFastMutex(&lk);MmUnmapLockedPages(uva,m);MmUnlockPages(m);IoFreeMdl(m);return STATUS_TOO_MANY_COMMANDS;}
    sl->uva=(ULONG_PTR)uva;sl->tva=va;sl->pa=pa;sl->sz=sz;sl->pid=(ULONG)(ULONG_PTR)PsGetProcessId(tp);
    sl->f=f;sl->mdl=m;sl->tp=tp;ObReferenceObject(tp);
    if(os)*os=sl;ExReleaseFastMutex(&lk);
    return STATUS_SUCCESS;
}

static NTSTATUS HM(PIRP I,PIO_STACK_LOCATION S) {
    MREQ* r=I->UserBuffer;if(!r)return STATUS_INVALID_PARAMETER;
    PEPROCESS p;NTSTATUS st=PsLookupProcessByProcessId(ULongToHandle(r->pid),&p);
    if(!NT_SUCCESS(st))return st;
    MRSP rs;st=m1(p,r->va,r->sz,r->f,&rs,NULL);ObDereferenceObject(p);
    *(MRSP*)I->UserBuffer=rs;I->IoStatus.Status=st;I->IoStatus.Information=sizeof(rs);return st;
}

static NTSTATUS HU(PIRP I,PIO_STACK_LOCATION S) {
    ULONGLONG v=(ULONG_PTR)I->UserBuffer;if(!v)return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&lk);ME* e=fva(v);
    if(!e){ExReleaseFastMutex(&lk);return STATUS_NOT_FOUND;}
    fe(e);ExReleaseFastMutex(&lk);
    I->IoStatus.Status=STATUS_SUCCESS;return STATUS_SUCCESS;
}

static NTSTATUS HC(PIRP I,PIO_STACK_LOCATION S) {
    CREQ* r=(CREQ*)I->AssociatedIrp.SystemBuffer;
    CRSP* s=(CRSP*)I->AssociatedIrp.SystemBuffer;RtlZeroMemory(s,sizeof(*s));
    if(!r||!r->d||r->d>16)return STATUS_INVALID_PARAMETER;
    PEPROCESS p;NTSTATUS st=PsLookupProcessByProcessId(ULongToHandle(r->pid),&p);
    if(!NT_SUCCESS(st)){s->st=st;goto d;}
    KAPC_STATE apc;KeStackAttachProcess(p,&apc);
    ULONGLONG ad=r->mod;
    for(ULONG i=0;i<r->d;i++){
        ULONGLONG ta=ad+r->o[i];s->l[i]=ta;
        if(i==r->d-1){ad=ta;break;}
        __try{ad=*(volatile ULONGLONG*)ta;if(!ad){st=STATUS_ACCESS_VIOLATION;break;}}__except(1){st=GetExceptionCode();break;}
    }
    KeUnstackDetachProcess(&apc);ObDereferenceObject(p);
    s->fin=ad;s->st=st;
d: I->IoStatus.Status=STATUS_SUCCESS;I->IoStatus.Information=sizeof(*s);return STATUS_SUCCESS;
}

static NTSTATUS HS(PIRP I,PIO_STACK_LOCATION S) {
    SREQ* r=(SREQ*)I->AssociatedIrp.SystemBuffer;
    SRSP* s=(SRSP*)I->AssociatedIrp.SystemBuffer;RtlZeroMemory(s,sizeof(*s));
    if(!r)return STATUS_INVALID_PARAMETER;
    PEPROCESS p;NTSTATUS st=PsLookupProcessByProcessId(ULongToHandle(r->pid),&p);
    if(!NT_SUCCESS(st))return st;
    KAPC_STATE apc;KeStackAttachProcess(p,&apc);
    ULONG mx=r->mx?r->mx:256;ULONG f=r->f;ULONG n=0;
    for(ULONGLONG a=r->sa;a<r->ea&&n<mx;a++){
        if(a==r->ex)continue;BOOLEAN ok=FALSE;
        __try{
            if(f==0){ULONG plen=r->pl;if(!plen||plen>64)break;ok=TRUE;
                for(ULONG i=0;i<plen;i++){UCHAR v=*(volatile UCHAR*)(a+i);
                    if(r->pat[i].m&&(v&r->pat[i].m)!=(r->pat[i].v&r->pat[i].m)){ok=FALSE;break;}}}
            else if(f==1){if(*(volatile LONG*)a==*(LONG*)&r->pat[0])ok=TRUE;}
            else if(f==2){if(*(volatile float*)a==*(float*)&r->pat[0])ok=TRUE;}
            else if(f==3){if(*(volatile LONGLONG*)a==*(LONGLONG*)&r->pat[0])ok=TRUE;}
        }__except(1){continue;}
        if(ok){s->r[n].a=a;n++;if(f&&f<4)a+=3;}
    }
    KeUnstackDetachProcess(&apc);ObDereferenceObject(p);
    s->cnt=n;I->IoStatus.Status=STATUS_SUCCESS;I->IoStatus.Information=sizeof(*s);return STATUS_SUCCESS;
}

static NTSTATUS HMd(PIRP I,PIO_STACK_LOCATION S) {
    MODR* r=(MODR*)I->AssociatedIrp.SystemBuffer;
    MODS* s=(MODS*)I->AssociatedIrp.SystemBuffer;RtlZeroMemory(s,sizeof(*s));
    if(!r)return STATUS_INVALID_PARAMETER;
    PEPROCESS p;NTSTATUS st=PsLookupProcessByProcessId(ULongToHandle(r->pid),&p);
    if(!NT_SUCCESS(st)){s->st=st;goto d;}
    KAPC_STATE apc;KeStackAttachProcess(p,&apc);
    __try{
        ULONG_PTR peb=*(ULONG_PTR*)((ULONG_PTR)p+0x550);
        if(!peb){st=STATUS_UNSUCCESSFUL;goto dd;}
        ULONG_PTR ldr=*(ULONG_PTR*)(peb+0x018);
        if(!ldr){st=STATUS_UNSUCCESSFUL;goto dd;}
        ULONG_PTR fl=*(ULONG_PTR*)(ldr+0x010);ULONG_PTR hd=fl;
        while(1){
            ULONG_PTR en=fl-0x010;
            ULONG_PTR db=*(ULONG_PTR*)(en+0x030);
            ULONG si=*(ULONG*)(en+0x040);
            ULONG_PTR np=*(ULONG_PTR*)(en+0x048);
            ULONG nl=*(USHORT*)(en+0x048);
            if(np&&nl>0&&db){
                WCHAR fn[64];ULONG cl=nl;if(cl>sizeof(fn)-2)cl=sizeof(fn)-2;
                RtlCopyMemory(fn,(PVOID)np,cl);fn[cl/2]=0;
                WCHAR* f1=fn;while(*f1)f1++;while(f1>fn&&f1[-1]!=L'\\')f1--;
                ULONG i=0;while(f1[i]&&r->n[i]&&f1[i]==r->n[i])i++;
                if(!f1[i]&&!r->n[i]){s->base=db;s->sz=si;s->st=STATUS_SUCCESS;break;}
            }
            fl=*(ULONG_PTR*)fl;if(fl==hd||!fl){st=STATUS_NOT_FOUND;break;}
        }
    }__except(1){st=GetExceptionCode();}
dd:KeUnstackDetachProcess(&apc);ObDereferenceObject(p);if(s->st!=STATUS_SUCCESS)s->st=st;
d:I->IoStatus.Status=STATUS_SUCCESS;I->IoStatus.Information=sizeof(*s);return STATUS_SUCCESS;
}

static NTSTATUS CC(PDEVICE_OBJECT D, PIRP I) {
    I->IoStatus.Status=STATUS_SUCCESS;I->IoStatus.Information=0;IoCompleteRequest(I,IO_NO_INCREMENT);return STATUS_SUCCESS;
}

static NTSTATUS DP(PDEVICE_OBJECT D, PIRP I) {
    PIO_STACK_LOCATION s=IoGetCurrentIrpStackLocation(I);NTSTATUS st;
    switch(s->Parameters.DeviceIoControl.IoControlCode){
    case MAP:st=HM(I,s);break;case UNM:st=HU(I,s);break;
    case CHN:st=HC(I,s);break;case SCN:st=HS(I,s);break;
    case MOD:st=HMd(I,s);break;
    default:I->IoStatus.Status=STATUS_INVALID_DEVICE_REQUEST;st=STATUS_INVALID_DEVICE_REQUEST;break;
    }
    IoCompleteRequest(I,IO_NO_INCREMENT);return st;
}

static VOID UL(PDRIVER_OBJECT D) {
    ExAcquireFastMutex(&lk);for(int i=0;i<MAXM;i++)if(gm[i].mdl)fe(&gm[i]);ExReleaseFastMutex(&lk);
    UNICODE_STRING sn;RtlInitUnicodeString(&sn,SYM);IoDeleteSymbolicLink(&sn);
    if(D->DeviceObject)IoDeleteDevice(D->DeviceObject);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT D,PUNICODE_STRING R) {
    PDEVICE_OBJECT dev=NULL;UNICODE_STRING dn,sn;
    RtlInitUnicodeString(&dn,DEV);NTSTATUS st=IoCreateDevice(D,0,&dn,0x8320,0x100,FALSE,&dev);
    if(!NT_SUCCESS(st))return st;
    RtlInitUnicodeString(&sn,SYM);st=IoCreateSymbolicLink(&sn,&dn);
    if(!NT_SUCCESS(st)){IoDeleteDevice(dev);return st;}
    ExInitializeFastMutex(&lk);RtlZeroMemory(gm,sizeof(gm));
    D->MajorFunction[IRP_MJ_CREATE]=CC;D->MajorFunction[IRP_MJ_CLOSE]=CC;
    D->MajorFunction[IRP_MJ_DEVICE_CONTROL]=DP;D->DriverUnload=UL;
    dev->Flags|=DO_DIRECT_IO;dev->Flags&=~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
