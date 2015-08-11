#include "driver.h"
#include "support.h"

#include <dpfilter.h>

#define STATUS_SUCCESS 0

#if DBG
// This is only to make linker happy. Enabling security checks
// causes linking a special library that expects that DriverEntry is present.
void DriverEntry()
{
    ERRORF("THIS SHOULD NOT BE CALLED, EVER");
}
#endif

// The driver function table with all function index/address pairs
static DRVFN g_DrvFunctions[] =
{
    { INDEX_DrvGetModes, (PFN)DrvGetModes },
    { INDEX_DrvSynchronizeSurface, (PFN)DrvSynchronizeSurface },
    { INDEX_DrvEnablePDEV, (PFN)DrvEnablePDEV },
    { INDEX_DrvCompletePDEV, (PFN)DrvCompletePDEV },
    { INDEX_DrvDisablePDEV, (PFN)DrvDisablePDEV },
    { INDEX_DrvEnableSurface, (PFN)DrvEnableSurface },
    { INDEX_DrvDisableSurface, (PFN)DrvDisableSurface },
    { INDEX_DrvAssertMode, (PFN)DrvAssertMode },
    { INDEX_DrvEscape, (PFN)DrvEscape }
};

// default mode, before gui agent connects
ULONG g_uDefaultWidth = 800;
ULONG g_uDefaultHeight = 600;
// initial second mode, will be updated by gui agent based on dom0 mode
ULONG g_uWidth = 1280;
ULONG g_uHeight = 800;
ULONG g_uBpp = 32;

// defines which GDI operations will call driver-defined callbacks
#define flGlobalHooks HOOK_SYNCHRONIZE

/******************************Public*Routine******************************\
* DrvEnableDriver
*
* Enables the driver by retrieving the drivers function table and version.
*
\**************************************************************************/
BOOL APIENTRY DrvEnableDriver(
    __in ULONG EngineVersion,
    __in ULONG cbEnableData,
    __in_bcount(cbEnableData) DRVENABLEDATA *EnableData
    )
{
    BOOL status = FALSE;
    FUNCTION_ENTER();

    if ((EngineVersion < DDI_DRIVER_VERSION_NT5) || (cbEnableData < sizeof(DRVENABLEDATA)))
    {
        ERRORF("Unsupported engine version %lu, DRVENABLEDATA size %lu", EngineVersion, cbEnableData);
        goto cleanup;
    }

    EnableData->pdrvfn = g_DrvFunctions;
    EnableData->c = sizeof(g_DrvFunctions) / sizeof(DRVFN);
    EnableData->iDriverVersion = DDI_DRIVER_VERSION_NT5;

    ReadRegistryConfig();
    status = TRUE;

cleanup:
    FUNCTION_EXIT();
    return status;
}

/******************************Public*Routine******************************\
* DrvEnablePDEV
*
* DDI function, Enables the Physical Device.
*
* Return Value: device handle to pdev.
*
\**************************************************************************/

DHPDEV APIENTRY DrvEnablePDEV(
    __in PDEVMODEW DevMode,	// Pointer to DEVMODE
    __in_opt PWCHAR LogicalAddress,	// Logical address, for printer drivers
    __in ULONG NumberPatterns,	// Number of patterns, for printer drivers
    __in_opt HSURF *StandardPatterns, // Return standard patterns, for printer drivers
    __in ULONG cbDevCaps, // Size of buffer pointed to by DevCaps
    __out_bcount(cbDevCaps) ULONG *DevCaps,	// Pointer to GDIINFO structure that describes device capabilities
    __in ULONG cbDevInfo, // Size of the following DEVINFO structure
    __out_bcount(cbDevInfo) PDEVINFO DevInfo, // Physical device information structure
    __in_opt HDEV EngDeviceHandle,	// GDI-supplied handle to the device, used for callbacks
    __in_opt WCHAR *DeviceName,	// User-readable device name
    __in HANDLE DisplayHandle // Display device handle
    )
{
    PQV_PDEV pdev = NULL;

    FUNCTION_ENTER();

    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(NumberPatterns);
    UNREFERENCED_PARAMETER(StandardPatterns);
    UNREFERENCED_PARAMETER(EngDeviceHandle);
    UNREFERENCED_PARAMETER(DeviceName);

    if (sizeof(DEVINFO) > cbDevInfo)
    {
        ERRORF("insufficient DEVINFO memory");
        goto cleanup;
    }

    if (sizeof(GDIINFO) > cbDevCaps)
    {
        ERRORF("insufficient GDIINFO memory");
        goto cleanup;
    }

    // Allocate a physical device structure.
    pdev = (PQV_PDEV)EngAllocMem(FL_ZERO_MEMORY, sizeof(QV_PDEV), ALLOC_TAG);
    if (pdev == NULL)
    {
        ERRORF("EngAllocMem(QV_PDEV) failed");
        goto cleanup;
    }

    pdev->DisplayHandle = DisplayHandle;

    // Get the current screen mode information. Set up device caps and devinfo.
    if (!InitPdev(pdev, DevMode, (PGDIINFO)DevCaps, DevInfo))
    {
        ERRORF("InitPdev() failed");
        EngFreeMem(pdev);
        pdev = NULL;
        goto cleanup;
    }

cleanup:
    FUNCTION_EXIT();
    return (DHPDEV)pdev;
}

/******************************Public*Routine******************************\
* DrvCompletePDEV
*
* Store the HDEV, the engines handle for this PDEV, in the DHPDEV.
*
\**************************************************************************/

VOID APIENTRY DrvCompletePDEV(
    __inout DHPDEV PhysicalDeviceHandle, // physical device handle created in DrvEnablePDEV
    __in HDEV EngDeviceHandle // GDI handle for pdev
    )
{
    FUNCTION_ENTER();
    ((PQV_PDEV)PhysicalDeviceHandle)->EngPdevHandle = EngDeviceHandle;
    FUNCTION_EXIT();
}

ULONG APIENTRY DrvGetModes(
    __in HANDLE DisplayHandle, // display device handle
    __in ULONG cbDevMode, // size of the DevMode buffer
    __out_bcount_opt(cbDevMode) PDEVMODEW DevMode // display mode array
    )
{
    ULONG bytesWritten = 0, bytesNeeded = 2 * sizeof(DEVMODEW); // 2 modes are supported
    ULONG returnValue;
    DWORD i;

    FUNCTION_ENTER();

    UNREFERENCED_PARAMETER(DisplayHandle);

    DEBUGF("DevMode: %p, size: %lu, bytes needed: %lu", DevMode, cbDevMode, bytesNeeded);
    if (DevMode == NULL) // return the required buffer size
    {
        returnValue = bytesNeeded;
    }
    else if (cbDevMode < bytesNeeded)
    {
        returnValue = 0;
    }
    else
    {
        bytesWritten = bytesNeeded;

        RtlZeroMemory(DevMode, bytesNeeded);
        for (i = 0; i < 2; i++)
        {
            memcpy(DevMode[i].dmDeviceName,
                   DLL_NAME,
                   sizeof(DevMode[i].dmDeviceName) < sizeof(DLL_NAME) ? sizeof(DevMode[i].dmDeviceName) : sizeof(DLL_NAME));

            DevMode[i].dmSpecVersion = DM_SPECVERSION;
            DevMode[i].dmDriverVersion = DM_SPECVERSION;

            DevMode[i].dmDriverExtra = 0;
            DevMode[i].dmSize = sizeof(DEVMODEW);
            DevMode[i].dmBitsPerPel = g_uBpp;

            switch (i)
            {
            case 0: // default resolution
                DevMode[i].dmPelsWidth = g_uDefaultWidth;
                DevMode[i].dmPelsHeight = g_uDefaultHeight;
                break;
            case 1: // current resolution
                DevMode[i].dmPelsWidth = g_uWidth;
                DevMode[i].dmPelsHeight = g_uHeight;
                break;
            }

            DevMode[i].dmDisplayFrequency = 60;
            DevMode[i].dmDisplayFlags = 0;
            DevMode[i].dmPanningWidth = DevMode[i].dmPelsWidth;
            DevMode[i].dmPanningHeight = DevMode[i].dmPelsHeight;

            DevMode[i].dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY;
        }

        returnValue = bytesWritten;
    }

    DEBUGF("return %lu", returnValue);

    FUNCTION_EXIT();
    return returnValue;
}

/******************************Public*Routine******************************\
* DrvDisablePDEV
*
* Release the resources allocated in DrvEnablePDEV. If a surface has been
* enabled DrvDisableSurface will have already been called.
*
\**************************************************************************/

VOID APIENTRY DrvDisablePDEV(
    DHPDEV PhysicalDeviceHandle
    )
{
    PQV_PDEV pdev = (PQV_PDEV)PhysicalDeviceHandle;

    FUNCTION_ENTER();
    EngDeletePalette(pdev->DefaultPalette);
    RtlZeroMemory(pdev, sizeof(QV_PDEV));
    EngFreeMem(pdev);
    FUNCTION_EXIT();
}

ULONG AllocateSurfaceMemory(
    __inout PQV_SURFACE Surface,
    __in ULONG PixelDataSize
    )
{
    QVMINI_ALLOCATE_MEMORY_RESPONSE *QvminiAllocateMemoryResponse = NULL;
    QVMINI_ALLOCATE_MEMORY QvminiAllocateMemory;
    ULONG returned;
    DWORD status = STATUS_NO_MEMORY;

    FUNCTION_ENTER();

    QvminiAllocateMemoryResponse = (QVMINI_ALLOCATE_MEMORY_RESPONSE *)
        EngAllocMem(FL_ZERO_MEMORY, sizeof(QVMINI_ALLOCATE_MEMORY_RESPONSE), ALLOC_TAG);

    if (!QvminiAllocateMemoryResponse)
    {
        ERRORF("EngAllocMem(QVMINI_ALLOCATE_MEMORY_RESPONSE) failed");
        goto cleanup;
    }

    QvminiAllocateMemory.Size = PixelDataSize;

    DEBUGF("calling IOCTL_QVMINI_ALLOCATE_MEMORY: size %lu", PixelDataSize);
    status = EngDeviceIoControl(
        Surface->Pdev->DisplayHandle,
        IOCTL_QVMINI_ALLOCATE_MEMORY,
        &QvminiAllocateMemory,
        sizeof(QvminiAllocateMemory),
        QvminiAllocateMemoryResponse,
        sizeof(QVMINI_ALLOCATE_MEMORY_RESPONSE),
        &returned);

    if (STATUS_SUCCESS != status)
    {
        ERRORF("EngDeviceIoControl(IOCTL_QVMINI_ALLOCATE_MEMORY) failed: 0x%lx", status);
        EngFreeMem(QvminiAllocateMemoryResponse);
        goto cleanup;
    }

    Surface->PixelData = QvminiAllocateMemoryResponse->VirtualAddress;
    Surface->PfnArray = QvminiAllocateMemoryResponse->PfnArray;

    EngFreeMem(QvminiAllocateMemoryResponse);
    status = STATUS_SUCCESS;

cleanup:
    FUNCTION_EXIT();
    return status;
}

ULONG AllocateSection(
    __inout PQV_SURFACE Surface,
    __in ULONG PixelDataSize
    )
{
    QVMINI_ALLOCATE_SECTION_RESPONSE *QvminiAllocateSectionResponse = NULL;
    QVMINI_ALLOCATE_SECTION QvminiAllocateSection;
    ULONG returned;
    DWORD status = STATUS_NO_MEMORY;

    FUNCTION_ENTER();

    QvminiAllocateSectionResponse = (QVMINI_ALLOCATE_SECTION_RESPONSE *)
        EngAllocMem(FL_ZERO_MEMORY, sizeof(QVMINI_ALLOCATE_SECTION_RESPONSE), ALLOC_TAG);

    if (!QvminiAllocateSectionResponse)
    {
        ERRORF("EngAllocMem(QVMINI_ALLOCATE_SECTION_RESPONSE) failed");
        goto cleanup;
    }

    QvminiAllocateSection.Size = PixelDataSize;
    QvminiAllocateSection.UseDirtyBits = g_bUseDirtyBits;

    DEBUGF("calling IOCTL_QVMINI_ALLOCATE_SECTION: size %lu, use dirty bits: %d", PixelDataSize, g_bUseDirtyBits);
    status = EngDeviceIoControl(
        Surface->Pdev->DisplayHandle,
        IOCTL_QVMINI_ALLOCATE_SECTION,
        &QvminiAllocateSection,
        sizeof(QvminiAllocateSection),
        QvminiAllocateSectionResponse,
        sizeof(QVMINI_ALLOCATE_SECTION_RESPONSE),
        &returned);

    if (STATUS_SUCCESS != status)
    {
        ERRORF("EngDeviceIoControl(IOCTL_QVMINI_ALLOCATE_SECTION) failed: 0x%lx", status);
        EngFreeMem(QvminiAllocateSectionResponse);
        goto cleanup;
    }

    Surface->PixelData = QvminiAllocateSectionResponse->VirtualAddress;
    Surface->Section = QvminiAllocateSectionResponse->Section;
    Surface->SectionObject = QvminiAllocateSectionResponse->SectionObject;
    Surface->Mdl = QvminiAllocateSectionResponse->Mdl;

    Surface->DirtySectionObject = QvminiAllocateSectionResponse->DirtySectionObject;
    Surface->DirtySection = QvminiAllocateSectionResponse->DirtySection;
    Surface->DirtyPages = QvminiAllocateSectionResponse->DirtyPages;

    Surface->PfnArray = QvminiAllocateSectionResponse->PfnArray;

    EngFreeMem(QvminiAllocateSectionResponse);
    status = STATUS_SUCCESS;

cleanup:
    FUNCTION_EXIT();
    return status;
}

ULONG AllocateSurfaceData(
    __in BOOLEAN IsSurface,
    __inout PQV_SURFACE Surface,
    __in ULONG PixelDataSize
    )
{
    ULONG status;

    FUNCTION_ENTER();

    if (IsSurface)
        status = AllocateSection(Surface, PixelDataSize);
    else
        status = AllocateSurfaceMemory(Surface, PixelDataSize);

    FUNCTION_EXIT();
    return status;
}

VOID FreeSurfaceMemory(
    __inout PQV_SURFACE Surface
    )
{
    DWORD status;
    QVMINI_FREE_MEMORY QvminiFreeMemory;
    ULONG returned;

    FUNCTION_ENTER();

    DEBUGF("surface: %p, data: %p", Surface, Surface->PixelData);

    QvminiFreeMemory.VirtualAddress = Surface->PixelData;
    QvminiFreeMemory.PfnArray = Surface->PfnArray;

    status = EngDeviceIoControl(Surface->Pdev->DisplayHandle,
                                IOCTL_QVMINI_FREE_MEMORY, &QvminiFreeMemory, sizeof(QvminiFreeMemory), NULL, 0, &returned);

    if (STATUS_SUCCESS != status)
    {
        ERRORF("EngDeviceIoControl(IOCTL_QVMINI_FREE_MEMORY) failed: 0x%lx", status);
    }

    FUNCTION_EXIT();
}

VOID FreeSection(
    __inout PQV_SURFACE Surface
    )
{
    DWORD status;
    QVMINI_FREE_SECTION QvminiFreeSection;
    ULONG returned;

    FUNCTION_ENTER();

    DEBUGF("surface: %p, data: %p", Surface, Surface->PixelData);

    QvminiFreeSection.VirtualAddress = Surface->PixelData;
    QvminiFreeSection.Section = Surface->Section;
    QvminiFreeSection.SectionObject = Surface->SectionObject;
    QvminiFreeSection.Mdl = Surface->Mdl;

    QvminiFreeSection.DirtyPages = Surface->DirtyPages;
    QvminiFreeSection.DirtySection = Surface->DirtySection;
    QvminiFreeSection.DirtySectionObject = Surface->DirtySectionObject;

    QvminiFreeSection.PfnArray = Surface->PfnArray;

    status = EngDeviceIoControl(Surface->Pdev->DisplayHandle,
                                IOCTL_QVMINI_FREE_SECTION, &QvminiFreeSection, sizeof(QvminiFreeSection), NULL, 0, &returned);

    if (0 != status)
    {
        ERRORF("EngDeviceIoControl(IOCTL_QVMINI_FREE_SECTION) failed: 0x%lx", status);
    }

    FUNCTION_EXIT();
}

VOID FreeSurfaceData(
    __inout PQV_SURFACE Surface
    )
{
    FUNCTION_ENTER();

    DEBUGF("surface: %p, screen? %d", Surface, Surface->IsScreen);
    if (Surface->IsScreen)
        FreeSection(Surface);
    else
        FreeSurfaceMemory(Surface);

    FUNCTION_EXIT();
}

VOID FreeSurface(
    __inout PQV_SURFACE Surface
    )
{
    FUNCTION_ENTER();

    if (!Surface)
        goto cleanup;

    DEBUGF("surface: %p", Surface);
    if (Surface->DriverObj) // user mode client didn't explicitely clean up
    {
        // Require a cleanup callback to be called.
        // surface->DriverObj will be set to NULL by the callback.
        EngDeleteDriverObj(Surface->DriverObj, TRUE, FALSE);
    }

    FreeSurfaceData(Surface);
    RtlZeroMemory(Surface, sizeof(QV_SURFACE));
    EngFreeMem(Surface);

cleanup:
    FUNCTION_EXIT();
}

ULONG AllocateNonOpaqueDeviceSurfaceOrBitmap(
    BOOLEAN IsSurface,
    HDEV EngDeviceHandle,
    ULONG BitCount,
    SIZEL Size,
    ULONG Hooks,
    __out HSURF *EngSurfaceHandle,
    __out PQV_SURFACE *Surface,
    PQV_PDEV Pdev
    )
{
    ULONG bitmapType;
    ULONG pixelDataSize;
    QV_SURFACE *surface;
    ULONG stride;
    HSURF surfaceHandle;
    DHSURF deviceSurfaceHandle;
    ULONG status = STATUS_INVALID_PARAMETER;

    FUNCTION_ENTER();

    if (!EngDeviceHandle || !EngSurfaceHandle || !Surface)
        goto cleanup;

    DEBUGF("IsSurface %d, bpp %lu, %dx%d", IsSurface, BitCount, Size.cx, Size.cy);

    switch (BitCount)
    {
    case 8:
        bitmapType = BMF_8BPP;
        break;
    case 16:
        bitmapType = BMF_16BPP;
        break;
    case 24:
        bitmapType = BMF_24BPP;
        break;
    case 32:
        bitmapType = BMF_32BPP;
        break;
    default:
        goto cleanup;
    }

    stride = (BitCount >> 3) * Size.cx;

    pixelDataSize = (ULONG)(stride * Size.cy);

    DEBUGF("allocating pixel data: %d x %d @ %lu, size %lu", Size.cx, Size.cy, BitCount, pixelDataSize);

    status = STATUS_NO_MEMORY;
    surface = (PQV_SURFACE)EngAllocMem(FL_ZERO_MEMORY, sizeof(QV_SURFACE), ALLOC_TAG);
    if (!surface)
    {
        ERRORF("EngAllocMem(QV_SURFACE) failed");
        goto cleanup;
    }

    surface->Pdev = Pdev;

    // surface->Pdev must be set before calling this,
    // because we query our miniport through EngDeviceIoControl().
    status = AllocateSurfaceData(IsSurface, surface, pixelDataSize);
    if (STATUS_SUCCESS != status)
    {
        ERRORF("AllocateSurfaceData() failed: 0x%lx", status);
        EngFreeMem(surface);
        goto cleanup;
    }

    // Create a surface.
    deviceSurfaceHandle = (DHSURF)surface;

    if (IsSurface)
        surfaceHandle = EngCreateDeviceSurface(deviceSurfaceHandle, Size, bitmapType);
    else
        surfaceHandle = (HSURF)EngCreateDeviceBitmap(deviceSurfaceHandle, Size, bitmapType);

    status = STATUS_INVALID_HANDLE;
    if (!surfaceHandle)
    {
        ERRORF("EngCreateDevice*() failed");
        FreeSurface(surface);
        goto cleanup;
    }

    // set surface to be device-managed
    if (!EngModifySurface(surfaceHandle, EngDeviceHandle, Hooks, 0, (DHSURF)deviceSurfaceHandle, surface->PixelData, stride, NULL))
    {
        ERRORF("EngModifySurface() failed");
        EngDeleteSurface(surfaceHandle);
        FreeSurface(surface);
        goto cleanup;
    }

    surface->Width = Size.cx;
    surface->Height = Size.cy;
    surface->Delta = stride;
    surface->BitCount = BitCount;

    if (Size.cx > 50)
    {
        DEBUGF("Surface %dx%d, data at %p (%lu bytes), pfns: %lu",
               Size.cx, Size.cy, surface->PixelData, pixelDataSize,
               surface->PfnArray->NumberOf4kPages);
    }

    *Surface = surface;
    *EngSurfaceHandle = surfaceHandle;
    status = STATUS_SUCCESS;

cleanup:
    FUNCTION_EXIT();
    return status;
}

// Set up a surface to be drawn on and associate it with a given physical device.
HSURF APIENTRY DrvEnableSurface(
    __inout DHPDEV PhysicalDeviceHandle
    )
{
    PQV_PDEV pdev;
    HSURF engSurfaceHandle;
    SIZEL screenSize;
    PQV_SURFACE surface = NULL;
    ULONG status;

    FUNCTION_ENTER();

    DEBUGF("pdev %p", PhysicalDeviceHandle);
    // Create engine bitmap around frame buffer.
    pdev = (QV_PDEV *)PhysicalDeviceHandle;

    screenSize.cx = pdev->ScreenWidth;
    screenSize.cy = pdev->ScreenHeight;

    status = AllocateNonOpaqueDeviceSurfaceOrBitmap(
        TRUE, pdev->EngPdevHandle, pdev->BitsPerPel, screenSize, flGlobalHooks, &engSurfaceHandle, &surface, pdev);

    if (status != STATUS_SUCCESS)
    {
        ERRORF("AllocateNonOpaqueDeviceSurfaceOrBitmap() failed: 0x%lx", status);
        return NULL;
    }

    pdev->EngSurfaceHandle = (HSURF)engSurfaceHandle;
    pdev->ScreenSurface = (PVOID)surface;

    surface->IsScreen = TRUE;

    FUNCTION_EXIT();
    return engSurfaceHandle;
}

/******************************Public*Routine******************************\
* DrvDisableSurface
*
* Free resources allocated by DrvEnableSurface. Release the surface.
*
\**************************************************************************/

VOID APIENTRY DrvDisableSurface(
    __inout DHPDEV PhysicalDeviceHandle
    )
{
    PQV_PDEV pdev = (PQV_PDEV)PhysicalDeviceHandle;

    FUNCTION_ENTER();

    EngDeleteSurface(pdev->EngSurfaceHandle);
    FreeSurface(pdev->ScreenSurface);
    pdev->ScreenSurface = NULL;

    FUNCTION_EXIT();
}

HBITMAP APIENTRY DrvCreateDeviceBitmap(
    __inout DHPDEV PhysiaclDeviceHandle,
    __in SIZEL BitmapSize,
    __in ULONG BitmapFormat // bits per pixel
    )
{
    PQV_SURFACE surface = NULL;
    ULONG bitCount;
    HSURF surfaceHandle = NULL;
    ULONG status;
    PQV_PDEV pdev = (PQV_PDEV)PhysiaclDeviceHandle;

    FUNCTION_ENTER();
    DEBUGF("%dx%d, format %lu", BitmapSize.cx, BitmapSize.cy, BitmapFormat);

    switch (BitmapFormat)
    {
    case BMF_8BPP:
        bitCount = 8;
        break;
    case BMF_16BPP:
        bitCount = 16;
        break;
    case BMF_24BPP:
        bitCount = 24;
        break;
    case BMF_32BPP:
        bitCount = 32;
        break;
    default:
        goto cleanup;
    }

    status = AllocateNonOpaqueDeviceSurfaceOrBitmap(
        FALSE, pdev->EngPdevHandle, bitCount, BitmapSize, flGlobalHooks, &surfaceHandle, &surface, pdev);

    if (status != STATUS_SUCCESS)
    {
        ERRORF("AllocateNonOpaqueDeviceSurfaceOrBitmap() failed: 0x%lx", status);
        goto cleanup;
    }

    surface->IsScreen = FALSE;

cleanup:
    FUNCTION_EXIT();
    return (HBITMAP)surfaceHandle;
}

VOID APIENTRY DrvDeleteDeviceBitmap(
    __inout DHSURF DeviceSurfaceHandle
    )
{
    PQV_SURFACE surface;

    FUNCTION_ENTER();

    surface = (PQV_SURFACE)DeviceSurfaceHandle;
    FreeSurface(surface);

    FUNCTION_EXIT();
}

BOOL APIENTRY DrvAssertMode(
    DHPDEV PhysicalDeviceHandle,
    BOOL Enable
    )
{
    FUNCTION_ENTER();

    // FIXME: is this required to support multiple desktops?
    // this function should switch resolution
    DEBUGF("pdev %p, enable %d", PhysicalDeviceHandle, Enable);

    FUNCTION_EXIT();
    return TRUE;
}

ULONG UserSupportVideoMode(
    __in ULONG cbInput, // size of the input buffer
    __in_bcount(cbInput) QV_SUPPORT_MODE *QvSupportMode
    )
{
    ULONG status = QV_INVALID_PARAMETER;

    FUNCTION_ENTER();

    if (cbInput < sizeof(QV_SUPPORT_MODE) || !QvSupportMode)
        goto cleanup;

    status = QV_SUPPORT_MODE_INVALID_RESOLUTION;
    if (!IS_RESOLUTION_VALID(QvSupportMode->Width, QvSupportMode->Height))
        goto cleanup;

    status = QV_SUPPORT_MODE_INVALID_BPP;
    if (QvSupportMode->Bpp != 16 && QvSupportMode->Bpp != 24 && QvSupportMode->Bpp != 32)
        goto cleanup;

    DEBUGF("%lu x %lu @ %lu)", QvSupportMode->Width, QvSupportMode->Height, QvSupportMode->Bpp);
    g_uWidth = QvSupportMode->Width;
    g_uHeight = QvSupportMode->Height;
    g_uBpp = QvSupportMode->Bpp;
    status = QV_SUCCESS;

cleanup:
    FUNCTION_EXIT();
    return status;
}

ULONG UserGetSurfaceData(
    __in PQV_SURFACE Surface,
    __in ULONG cbInput,
    __in_bcount(cbInput) QV_GET_SURFACE_DATA *QvGetSurfaceData,
    __in ULONG cbOutput,
    __out_bcount(cbOutput) QV_GET_SURFACE_DATA_RESPONSE *QvGetSurfaceDataResponse
    )
{
    ULONG status = QV_INVALID_PARAMETER;

    FUNCTION_ENTER();

    if (cbOutput < sizeof(QV_GET_SURFACE_DATA_RESPONSE) || !QvGetSurfaceDataResponse
        || cbInput < sizeof(QV_GET_SURFACE_DATA) || !QvGetSurfaceData)
        goto cleanup;

    QvGetSurfaceDataResponse->Magic = QVIDEO_MAGIC;

    QvGetSurfaceDataResponse->Width = Surface->Width;
    QvGetSurfaceDataResponse->Height = Surface->Height;
    QvGetSurfaceDataResponse->Delta = Surface->Delta;
    QvGetSurfaceDataResponse->Bpp = Surface->BitCount;
    QvGetSurfaceDataResponse->IsScreen = Surface->IsScreen;

    DEBUGF("PFN memcpy(%p, size %lu)", QvGetSurfaceData->PfnArray, PFN_ARRAY_SIZE(Surface->Width, Surface->Height));
    memcpy(QvGetSurfaceData->PfnArray, Surface->PfnArray,
           PFN_ARRAY_SIZE(Surface->Width, Surface->Height));

    status = QV_SUCCESS;

cleanup:
    FUNCTION_EXIT();
    return status;
}

// This function is called when a client process (gui agent) terminates without cleaning up first.
// This function frees up resource(s) registered by EngCreateDriverObj (in this case, damage notification event).
BOOL CALLBACK ProcessCleanup(
    __in DRIVEROBJ *DriverObj
    )
{
    PQV_SURFACE surface = NULL;

    FUNCTION_ENTER();

    surface = (PQV_SURFACE)DriverObj->pvObj; // set when a client starts watching surface
    DEBUGF("surface %p, unmapping event %p", surface, surface->DamageNotificationEvent);

    EngUnmapEvent(surface->DamageNotificationEvent);

    surface->DamageNotificationEvent = NULL;
    surface->DriverObj = NULL;

    FUNCTION_EXIT();
    return TRUE;
}

ULONG UserWatchSurface(
    __inout PQV_SURFACE Surface,
    __in HDEV EngDeviceHandle,
    __in ULONG cbInput,
    __in_bcount(cbInput) QV_WATCH_SURFACE *QvWatchSurface
    )
{
    PEVENT damageNotificationEvent = NULL;
    ULONG status = QV_INVALID_PARAMETER;

    FUNCTION_ENTER();

    if (cbInput < sizeof(QV_WATCH_SURFACE) || !QvWatchSurface)
        goto cleanup;

    DEBUGF("surface: %p, event: %p", Surface, QvWatchSurface->UserModeEvent);

    if (Surface->DriverObj)
    {
        DEBUGF("Surface %p is already watched", Surface);
        goto cleanup;
    }

    status = QV_INVALID_HANDLE;
    damageNotificationEvent = EngMapEvent(EngDeviceHandle, QvWatchSurface->UserModeEvent, NULL, NULL, NULL);
    if (!damageNotificationEvent)
    {
        ERRORF("EngMapEvent(%p) failed: 0x%lx", QvWatchSurface->UserModeEvent, EngGetLastError());
        goto cleanup;
    }

    DEBUGF("event: %p", damageNotificationEvent);

    // *Maybe* this surface can be accessed by UnmapDamageNotificationEventProc() or DrvSynchronizeSurface() right after EngCreateDriverObj(),
    // so pSurfaceDescriptor->pDamageNotificationEvent must be set before calling it.
    Surface->DamageNotificationEvent = damageNotificationEvent;

    // Install a notification callback for process deletion.
    // We have to unmap the event if the client process terminates unexpectedly.
    Surface->DriverObj = EngCreateDriverObj(Surface, ProcessCleanup, EngDeviceHandle);
    if (!Surface->DriverObj)
    {
        ERRORF("EngCreateDriverObj(%p) failed: 0x%lx", Surface, EngGetLastError());

        Surface->DamageNotificationEvent = NULL;
        EngUnmapEvent(damageNotificationEvent);
        goto cleanup;
    }

    DEBUGF("DriverObj: %p", Surface->DriverObj);
    status = QV_SUCCESS;

cleanup:
    FUNCTION_EXIT();
    return status;
}

ULONG UserStopWatchingSurface(
    __inout PQV_SURFACE Surface
    )
{
    ULONG status = QV_INVALID_PARAMETER;

    FUNCTION_ENTER();

    if (!Surface->DriverObj)
    {
        WARNINGF("DriverObj is zero");
        goto cleanup;
    }

    DEBUGF("surface: %p", Surface);

    // Require a cleanup callback to be called.
    // DriverObj will be set to NULL by the callback.
    EngDeleteDriverObj(Surface->DriverObj, TRUE, FALSE);
    status = QV_SUCCESS;

cleanup:
    FUNCTION_EXIT();
    return status;
}

ULONG APIENTRY DrvEscape(
    __inout SURFOBJ *SurfaceObject,
    __in ULONG EscapeCode,
    __in ULONG cbInput,
    __in_bcount(cbInput) PVOID InputBuffer,
    __in ULONG cbOutput,
    __out_bcount(cbOutput) PVOID OutputBuffer
    )
{
    PQV_SURFACE surface;

    FUNCTION_ENTER();

    DEBUGF("surfobj: %p, code: 0x%lx", SurfaceObject, EscapeCode);
    if ((cbInput < sizeof(ULONG)) || !InputBuffer || (*(PULONG)InputBuffer != QVIDEO_MAGIC))
    {
        WARNINGF("bad size/magic");
        return QV_INVALID_PARAMETER;
    }

    surface = (PQV_SURFACE)SurfaceObject->dhsurf;
    if (!surface || !surface->Pdev)
    {
        WARNINGF("invalid surface");
        return QV_INVALID_PARAMETER;
    }
    // FIXME: validate user buffers!

    switch (EscapeCode)
    {
    case QVESC_SUPPORT_MODE:
        return UserSupportVideoMode(cbInput, InputBuffer);

    case QVESC_GET_SURFACE_DATA:
        return UserGetSurfaceData(surface, cbInput, InputBuffer, cbOutput, OutputBuffer);

    case QVESC_WATCH_SURFACE:
        return UserWatchSurface(surface, SurfaceObject->hdev, cbInput, InputBuffer);

    case QVESC_STOP_WATCHING_SURFACE:
        return UserStopWatchingSurface(surface);

    case QVESC_SYNCHRONIZE:
        if (!g_bUseDirtyBits)
            return QV_NOT_SUPPORTED;

        InterlockedExchange(&surface->DirtyPages->Ready, 1);
        TRACEF("gui agent synchronized");
        return QV_SUCCESS;

    default:
        WARNINGF("bad code 0x%lx", EscapeCode);
        return QV_NOT_SUPPORTED;
    }
}

VOID APIENTRY DrvSynchronizeSurface(
    SURFOBJ *SurfaceObject,
    PRECTL DamageRectangle,
    FLONG Flag
    )
{
    PQV_SURFACE surface = NULL;
    ULONG surfaceBufferSize, dirty;

    FUNCTION_ENTER();

    UNREFERENCED_PARAMETER(DamageRectangle);
    UNREFERENCED_PARAMETER(Flag);

    if (!SurfaceObject)
        return;

    surface = (PQV_SURFACE)SurfaceObject->dhsurf;
    if (!surface)
        return;

    if (!surface->DamageNotificationEvent)
        // This surface is not being watched.
        return;

    surfaceBufferSize = surface->Delta * surface->Height;

    // UpdateDirtyBits returns 0 also if the check was too early after a previous one.
    // This just returns 1 if using dirty bits is disabled.
    dirty = UpdateDirtyBits(surface->PixelData, surfaceBufferSize, surface->DirtyPages);

    if (dirty > 0) // only signal the event if something changed
        EngSetEvent(surface->DamageNotificationEvent);

    FUNCTION_EXIT();
}
