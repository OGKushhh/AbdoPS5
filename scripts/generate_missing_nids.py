#!/usr/bin/env python3
"""
Generate NID hashes for common PS5 function names that aren't in nids.csv.
PS5 NIDs are SHA-1 hashes of the function name (first 8 bytes, base64-encoded).
This script generates NIDs for known PS5 function names from the SDK.
"""
import hashlib
import base64
import csv
import os
from pathlib import Path

# PS5 NID computation: SHA-1 of the function name, first 8 bytes, base64url-encoded
def compute_nid(name: str) -> str:
    sha1 = hashlib.sha1(name.encode('ascii')).digest()
    nid_bytes = sha1[:8]
    # PS5 uses base64url encoding (no padding)
    nid = base64.urlsafe_b64encode(nid_bytes).decode('ascii').rstrip('=')
    return nid

# Common PS5 function names that might not be in our database yet
# (extracted from PS5 SDK documentation and common patterns)
COMMON_PS5_FUNCTIONS = [
    # libkernel
    "sceKernelAllocateDirectMemory",
    "sceKernelMapDirectMemory",
    "sceKernelReleaseDirectMemory",
    "sceKernelQueryMemoryProtection",
    "sceKernelVirtualQuery",
    "sceKernelGetDirectMemorySize",
    "sceKernelGetDirectMemoryType",
    "sceKernelReserveVirtualRange",
    "sceKernelMapFlexibleMemory",
    "sceKernelAvailableFlexibleMemorySize",
    "sceKernelBatchMap",
    "sceKernelBatchMap2",
    "sceKernelMprotect",
    "sceKernelMunmap",
    "sceKernelMlock",
    "sceKernelMlockall",
    "sceKernelGetProcessTime",
    "sceKernelGetProcessTimeCounter",
    "sceKernelReadTsc",
    "sceKernelGetTscFrequency",
    # Audio
    "sceAudioOutInit",
    "sceAudioOutOpen",
    "sceAudioOutClose",
    "sceAudioOutOutput",
    "sceAudioOutOutputs",
    "sceAudioOutSetVolume",
    "sceAudioOutGetStatus",
    "sceAudioOutGetPortState",
    "sceAudioOut2Init",
    "sceAudioOut2Open",
    "sceAudioOut2Close",
    "sceAudioOut2Mix",
    # Video
    "sceVideoOutOpen",
    "sceVideoOutClose",
    "sceVideoOutSetBufferAttribute",
    "sceVideoOutRegisterBuffers",
    "sceVideoOutSubmitFlip",
    "sceVideoOutGetFlipStatus",
    "sceVideoOutGetVblankStatus",
    "sceVideoOutGetResolutionStatus",
    "sceVideoOutGetDeviceCapabilityInfo",
    # Pad
    "scePadInit",
    "scePadOpen",
    "scePadClose",
    "scePadRead",
    "scePadReadState",
    "scePadGetControllerInformation",
    "scePadSetVibration",
    "scePadSetLightBar",
    "scePadResetLightBar",
    "scePadSetTriggerEffect",
    # Gnm/AGC
    "sceAgcSubmitJob",
    "sceAgcSetGraphicsRegisterDefaults",
    "sceAgcSetComputeRegisterDefaults",
    "sceGnmSubmitCommandBuffers",
    "sceGnmSubmitCommandBuffersForReplay",
    # SaveData
    "sceSaveDataInitialize",
    "sceSaveDataOpen",
    "sceSaveDataRead",
    "sceSaveDataWrite",
    "sceSaveDataClose",
    "sceSaveDataDelete",
    # System
    "sceSystemServiceKillProcess",
    "sceSystemServiceGetStatus",
    "sceSystemServiceRequestPowerOff",
    "sceSystemServiceRequestReboot",
    "sceSystemServiceGetMaxWorkspaceBudget",
    "sceUserServiceInitialize",
    "sceUserServiceGetInitialUser",
    "sceUserServiceGetUserName",
    # PlayGo
    "scePlayGoInitialize",
    "scePlayGoOpen",
    "scePlayGoClose",
    "scePlayGoGetLocus",
    "scePlayGoTrigger",
    "scePlayGoStatus",
    # Network
    "sceNetInitialize",
    "sceNetTerminate",
    "sceNetSocket",
    "sceNetConnect",
    "sceNetBind",
    "sceNetListen",
    "sceNetAccept",
    "sceNetSend",
    "sceNetRecv",
    "sceNetClose",
    # Np
    "sceNpInitialize",
    "sceNpTerminate",
    "sceNpGetState",
    "sceNpGetNpId",
    "sceNpGetOnlineId",
    # Rtc
    "sceRtcGetCurrentTick",
    "sceRtcGetCurrentClockLocalTime",
    "sceRtcGetCurrentClock",
    "sceRtcSetTick",
    "sceRtcGetTick",
    # Ime
    "sceImeOpen",
    "sceImeClose",
    "sceImeUpdate",
    "sceImeSetText",
    "sceImeSetCaret",
    # AvPlayer
    "sceAvPlayerInit",
    "sceAvPlayerOpen",
    "sceAvPlayerClose",
    "sceAvPlayerGetVideoData",
    "sceAvPlayerGetAudioData",
    # Ajm
    "sceAjmInitialize",
    "sceAjmInstanceCreate",
    "sceAjmInstanceDestroy",
    "sceAjmBatchJobControl",
    "sceAjmBatchJobRun",
    "sceAjmBatchStart",
]

def main():
    repo = Path(__file__).resolve().parent.parent
    nids_csv = repo / "data" / "nids.csv"

    # Load existing NIDs
    existing_nids = set()
    existing_names = set()
    if nids_csv.exists():
        with open(nids_csv) as f:
            reader = csv.DictReader(f)
            for row in reader:
                existing_nids.add(row['nid'])
                existing_names.add(row['name'])

    print(f"Existing NIDs: {len(existing_nids)}")
    print(f"Existing names: {len(existing_names)}")

    # Generate NIDs for common functions
    new_entries = []
    for name in COMMON_PS5_FUNCTIONS:
        if name in existing_names:
            continue  # Already in database
        nid = compute_nid(name)
        if nid not in existing_nids:
            new_entries.append((nid, name, "generated"))
            existing_nids.add(nid)
            existing_names.add(name)

    print(f"Generated {len(new_entries)} new NID entries")

    if new_entries:
        # Append to CSV
        with open(nids_csv, 'a', newline='') as f:
            writer = csv.writer(f)
            for nid, name, source in new_entries:
                writer.writerow([nid, name, source])

        print(f"Appended to {nids_csv}")
        print(f"Total entries: {len(existing_nids)}")

        # Show sample
        print("\nSample generated entries:")
        for nid, name, _ in new_entries[:10]:
            print(f"  {nid} = {name}")
    else:
        print("No new entries to add — all common functions already in database.")

if __name__ == "__main__":
    main()
