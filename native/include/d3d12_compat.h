/*
 * d3d12_compat.h - ID3D12Device2 / ID3D12Device3 for toolchains whose <d3d12.h> stops at ID3D12Device1
 * (mingw-w64 11.0.1 of Ubuntu 24.04, for example). It is only used by the Windows helper (fsr4_bridge.exe).
 *
 * The interface layout (method order) and the IIDs follow Microsoft's d3d12.idl as published in
 * https://github.com/microsoft/DirectX-Headers (MIT License, Copyright (c) Microsoft Corporation).
 * Only the vtable layout matters for COM calls; parameter types that this project never uses are declared
 * with ABI-compatible stand-ins. When the real definitions exist (newer mingw-w64) this header does nothing.
 * See THIRD_PARTY_NOTICES.md.
 */
#pragma once
#include <d3d12.h>

#ifdef __cplusplus

#ifndef __ID3D12Device2_INTERFACE_DEFINED__
#define __ID3D12Device2_INTERFACE_DEFINED__
#define FSR4_COMPAT_DEVICE2 1
struct D3D12_PIPELINE_STATE_STREAM_DESC;   /* only ever passed as a pointer */
interface ID3D12Device2 : public ID3D12Device1 {
    virtual HRESULT STDMETHODCALLTYPE CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC *pDesc, REFIID riid, void **ppPipelineState) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(ID3D12Device2, 0x30baa41e, 0xb15b, 0x475c, 0xa0,0xbb, 0x1a,0xf5,0xc5,0xb6,0x43,0x28)
#endif
#endif

#ifndef __ID3D12Device3_INTERFACE_DEFINED__
#define __ID3D12Device3_INTERFACE_DEFINED__
#define FSR4_COMPAT_DEVICE3 1
interface ID3D12Device3 : public ID3D12Device2 {
    virtual HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress(const void *pAddress, REFIID riid, void **ppvHeap) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenExistingHeapFromFileMapping(HANDLE hFileMapping, REFIID riid, void **ppvHeap) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnqueueMakeResident(UINT Flags, UINT NumObjects, ID3D12Pageable *const *ppObjects, ID3D12Fence *pFenceToSignal, UINT64 FenceValueToSignal) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(ID3D12Device3, 0x81dadc15, 0x2bad, 0x4392, 0x93,0xc5, 0x10,0x13,0x45,0xc4,0xaa,0x98)
#endif
#endif

#endif /* __cplusplus */
